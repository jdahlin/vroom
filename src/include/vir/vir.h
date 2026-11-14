/*
 * VIR: the SSA intermediate representation used at -O2 (SPEC-vir.md).  This
 * is the public format: instruction, block, slot and function records
 * (section 3), types (4), opcodes (7.3, 8.1), flags (7.2), signature and group
 * encoding (9), the opcode table every tool reads, and the operations on a
 * function: construction, vir_cfg_build, vir_dominators_build, vir_uses_count and vir_canon.
 *
 * The module depends on nothing in frontend/c/, codegen/ or arch/.  Symbols, signatures
 * and asm templates of a text unit are in unit-local tables (vir_unit).
 *
 * Building a function (the builder form; the parser and the compiler's
 * builder use it):
 *
 *   vir_function_begin(&f, target, sym, fl);
 *   b = vir_block_add(&f, 0);			// opens block b: later appends go into it
 *   x = vir_insn_add(&f, VIR_OP_ADD, VIR_TYPE_I32, 0, p, q, 0);
 *   k = vir_constant_add(&f, VIR_OP_ICONST, VIR_TYPE_I32, 0, 5, 0);	// hash-consed (5.3)
 *   ...
 *   vir_canon(&f);				// 2.4: the form every pass receives
 *
 * In the builder form a constant may be created while a block is open: it
 * then lies inside that block's range (blk.insn_count counts it) and vir_canon moves
 * it to the constant region.  constant_count is 0 until then (verify rejects such a
 * function with S1).  The arrays of a function live in the thread's
 * vir_context (reserved once, reused by every function; nothing is allocated
 * or cleared per function); vir_function_begin invalidates the previous function.
 */
#ifndef VCC_VIR_H
#define VCC_VIR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/buffer.h"

/* ---- 4: types ---- */
enum { VIR_TYPE_VOID, VIR_TYPE_I32, VIR_TYPE_I64, VIR_TYPE_F32, VIR_TYPE_F64, VIR_TYPE_PTR, VIR_TYPE_MULTI,
       VIR_TYPE_F32X4, VIR_TYPE_I32X4, VIR_TYPE_F64X2, VIR_TYPE_I64X2,	/* 128-bit vectors (RESEARCH-vectors.md 5.1; i8x16 and i16x8 are not in yet) */
       VIR_TYPE_COUNT };
static inline bool vir_type_is_vector(uint32_t ty) { return ty >= VIR_TYPE_F32X4 && ty <= VIR_TYPE_I64X2; }
/* a vector's lane type; a scalar's own */
static inline uint32_t vir_type_element(uint32_t ty)
{
	return ty == VIR_TYPE_F32X4 ? VIR_TYPE_F32 : ty == VIR_TYPE_I32X4 ? VIR_TYPE_I32 : ty == VIR_TYPE_F64X2 ? VIR_TYPE_F64 : ty == VIR_TYPE_I64X2 ? VIR_TYPE_I64 : ty;
}
/* the integer vector with a vector's lane width (its mask), the float one; VIR_TYPE_VOID for a scalar */
static inline uint32_t vir_type_vector_integer(uint32_t ty) { return ty == VIR_TYPE_F32X4 || ty == VIR_TYPE_I32X4 ? VIR_TYPE_I32X4 : ty == VIR_TYPE_F64X2 || ty == VIR_TYPE_I64X2 ? VIR_TYPE_I64X2 : VIR_TYPE_VOID; }
static inline uint32_t vir_type_vector_float(uint32_t ty) { return ty == VIR_TYPE_F32X4 || ty == VIR_TYPE_I32X4 ? VIR_TYPE_F32X4 : ty == VIR_TYPE_F64X2 || ty == VIR_TYPE_I64X2 ? VIR_TYPE_F64X2 : VIR_TYPE_VOID; }

/* ---- 7.3, 8.1: opcodes ---- */
enum {
	VIR_OP_NOP = 0,
	VIR_OP_ICONST = 1, VIR_OP_FCONST = 2, VIR_OP_SYMADDR = 3, VIR_OP_SLOTADDR = 4, VIR_OP_LABELADDR = 5,
	VIR_OP_BPARAM = 6, VIR_OP_PARAM = 7, VIR_OP_PROJ = 8,
	VIR_OP_ADD = 10, VIR_OP_SUB = 11, VIR_OP_MUL = 12, VIR_OP_SDIV = 13, VIR_OP_UDIV = 14, VIR_OP_SREM = 15, VIR_OP_UREM = 16,
	VIR_OP_AND = 17, VIR_OP_OR = 18, VIR_OP_XOR = 19, VIR_OP_SHL = 20, VIR_OP_LSHR = 21, VIR_OP_ASHR = 22,
	VIR_OP_UMULHI = 23, VIR_OP_SMULHI = 24, VIR_OP_NEG = 25, VIR_OP_NOT = 26, VIR_OP_CLZ = 27, VIR_OP_CTZ = 28,
	VIR_OP_POPCNT = 29, VIR_OP_BSWAP = 30, VIR_OP_EXT = 31, VIR_OP_TRUNC = 32, VIR_OP_ICMP = 33, VIR_OP_FCMP = 34,
	VIR_OP_SELECT = 35, VIR_OP_FADD = 36, VIR_OP_FSUB = 37, VIR_OP_FMUL = 38, VIR_OP_FDIV = 39, VIR_OP_FNEG = 40,
	VIR_OP_FABS = 41, VIR_OP_FSQRT = 42, VIR_OP_FEXT = 43, VIR_OP_FTRUNC = 44, VIR_OP_S2F = 45, VIR_OP_U2F = 46,
	VIR_OP_F2S = 47, VIR_OP_F2U = 48, VIR_OP_BITCAST = 49, VIR_OP_PTRADD = 50, VIR_OP_PSUB = 51, VIR_OP_P2I = 52,
	VIR_OP_I2P = 53, VIR_OP_LOAD = 54, VIR_OP_STORE = 55, VIR_OP_MEMCPY = 56, VIR_OP_MEMSET = 57,
	VIR_OP_ALOAD = 58, VIR_OP_ASTORE = 59, VIR_OP_AXCHG = 60, VIR_OP_AXADD = 61, VIR_OP_ACAS = 62, VIR_OP_AFENCE = 63,
	VIR_OP_CALL = 64, VIR_OP_VA_START = 65, VIR_OP_VA_ARG = 66, VIR_OP_TLSADDR = 67, VIR_OP_ASM = 68,
	VIR_OP_BARRIER = 69, VIR_OP_FRAMEADDR = 70, VIR_OP_RETADDR = 71,
	VIR_OP_FMA = 9,		/* (72-79 and 87-88 are vector operations) */
	/* vectors: add sub mul and or xor neg not, the f* operations, select, load and store take vector
	 * types as they are; a mask is an i32x4 whose lanes are 0 or -1 */
	VIR_OP_VSPLAT = 72,		/* a in every lane */
	VIR_OP_VSERIES = 73,	/* lanes 0, 1, 2, ... */
	VIR_OP_VICMP = 74, VIR_OP_VFCMP = 75,	/* icmp's, fcmp's codes per lane: 0 or -1, in the integer vector of that lane width */
	VIR_OP_VBITSEL = 76,	/* per bit: a ? b : c */
	VIR_OP_VS2F = 77,		/* signed lanes to float of the same width */
	VIR_OP_VREV = 78,		/* the lanes in reverse order */
	VIR_OP_VREDUCE = 79,	/* the lanes' sum (i32x4 -> i32, i64x2 -> i64) */
	VIR_OP_VINSERT = 87, VIR_OP_VEXTRACT = 88, /* constant lane insert/extract */
	VIR_OP_BR = 80, VIR_OP_BRIF = 81, VIR_OP_SWITCH = 82, VIR_OP_IBR = 83, VIR_OP_ASMGOTO = 84, VIR_OP_RET = 85, VIR_OP_TRAP = 86,
	VIR_OP_CRC32C64 = 89, /* update a CRC32C with eight low-to-high bytes */
	VIR_OP_TARGET_FIRST = 128,	/* 128-255: target instructions introduced by lowering */
	VIR_OP_COUNT = 256
};

/* ---- 7.2: instruction flags ---- */
enum {
	VIR_INSN_FLAG_NSW = 1, VIR_INSN_FLAG_NUW = 2,		/* add sub mul shl neg */
	VIR_INSN_FLAG_EXACT = 4,				/* sdiv udiv lshr ashr */
	VIR_INSN_FLAG_NZ = 8,				/* clz ctz */
	VIR_INSN_FLAG_INB = 16,				/* ptradd: reserved, 0 */
	VIR_INSN_FLAG_MS = 4,				/* load, ext: sign-extend */
	VIR_INSN_FLAG_VOL = 0x100,			/* load store atomics: volatile */
	VIR_INSN_FLAG_CALL_NORETURN = 1, VIR_INSN_FLAG_CALL_PURE = 2, VIR_INSN_FLAG_CALL_CONST = 4, VIR_INSN_FLAG_CALL_TAIL = 8,
	VIR_INSN_FLAG_LIKELY = 1, VIR_INSN_FLAG_UNLIKELY = 2,	/* brif */
	VIR_INSN_FLAG_NEGP = 1, VIR_INSN_FLAG_NEGC = 2		/* fma: the product, the addend negated */
};
/* load store atomics: bits 0-1 log2 width, 4-7 log2 alignment */
#define VIR_INSN_FLAG_WIDTH_LOG2(fl)	((fl) & 3)
#define VIR_INSN_FLAG_ALIGN_LOG2(fl)	(((fl) >> 4) & 15)
#define VIR_INSN_FLAG_SOURCE_ALIGN_LOG2(fl)	(((fl) >> 8) & 15)	/* memcpy: the source's */

/* ---- 3.2, 3.3, 3.4: flags of blocks, slots, functions ---- */
enum { VIR_BLOCK_FLAG_LOOP = 1, VIR_BLOCK_FLAG_ADDR = 2, VIR_BLOCK_FLAG_COLD = 4 };
enum { VIR_SLOT_FLAG_PARAM = 1, VIR_SLOT_FLAG_EXPOSED = 2 };
enum {
	VIR_FUNCTION_FLAG_NOFP = 1, VIR_FUNCTION_FLAG_FENV = 2, VIR_FUNCTION_FLAG_NULLINVALID = 4, VIR_FUNCTION_FLAG_VARIADIC = 8, VIR_FUNCTION_FLAG_NORETURN = 16,
	VIR_FUNCTION_FLAG_LOCAL = 32,		/* the symbol is local to the unit (text form: `local`) */
	VIR_FUNCTION_FLAG_EXPOSURE = 64	/* VIR_SLOT_FLAG_EXPOSED of every slot is valid (computed by `mem`, checked by M1) */
};
enum { VIR_TARGET_NONE, VIR_TARGET_X86_64_LINUX, VIR_TARGET_A64_LINUX, VIR_TARGET_A64_DARWIN, VIR_TARGET_COUNT };

#define VIR_BLOCK_NONE 0xffffffffu

/* The analyses a function has computed and that nothing has invalidated since.  vir_cfg_build and
 * vir_dominators_build are the getters: they return at once when their bit is set and compute otherwise.
 * Whatever changes blocks or edges calls vir_analyses_invalidate (construction does). */
enum { VIR_VALID_CFG = 1, VIR_VALID_DOM = 2 };

/* ---- 3: the records ---- */
typedef struct vir_insn {
	uint8_t  op;	/* VO_* */
	uint8_t  type;	/* VT_* of the result */
	uint16_t flags;	/* per-op flags (7.2) */
	uint32_t a, b, c;	/* per op: a value, an immediate, a table index or an extra offset */
} vir_insn;
_Static_assert(sizeof(vir_insn) == 16, "vir_ins is 16 bytes (SPEC-vir.md 3.1)");

typedef struct vir_block {
	uint32_t first, insn_count;	/* instructions [first, first+n) */
	uint16_t parameter_count;	/* the first nparam instructions are VIR_OP_BPARAM */
	uint16_t flags;		/* VIR_BLOCK_FLAG_LOOP header, VIR_BLOCK_FLAG_ADDR address taken, VIR_BLOCK_FLAG_COLD */
	uint16_t depth;		/* loop nesting, 0 outside loops */
	uint16_t pad;
	uint32_t predecessor_first, predecessor_count;	/* predecessors: extra[pred .. pred+npred), block ids; valid after vir_cfg_build */
	uint32_t immediate_dominator;		/* immediate dominator (block 0: itself); VIR_BLOCK_NONE if unreachable; valid after vir_dominators_build */
	uint32_t preorder, preorder_last;	/* dominator-tree preorder number and the largest one in the subtree (22.5) */
} vir_block;
_Static_assert(sizeof(vir_block) == 36, "vir_blk is 36 bytes");

typedef struct vir_slot {
	uint64_t size;
	uint32_t align;		/* power of two */
	uint32_t flags;		/* VIR_SLOT_FLAG_PARAM incoming memory argument, VIR_SLOT_FLAG_EXPOSED (computed, 6.4) */
	int64_t  hint;		/* frame offset the builder proposes; INT64_MIN: none */
} vir_slot;
_Static_assert(sizeof(vir_slot) == 24, "vir_slot is 24 bytes");

/* 22.7: a run of instructions with one source location (loc: the compiler's id) */
typedef struct vir_location {
	uint32_t first;		/* first instruction index of the run */
	uint32_t source_location;
} vir_location;

typedef struct vir_function {
	uint32_t version;	/* 1 */
	uint32_t target;	/* VIR_TARGET_X86_64_LINUX, VIR_TARGET_A64_LINUX, VIR_TARGET_A64_DARWIN */
	uint32_t symbol;		/* the function's symbol (emitter ref) */
	uint32_t signature;		/* extra offset of its signature (section 9) */
	uint32_t flags;		/* VF_* */
	vir_insn  *insns;   uint32_t insn_count, constant_count;	/* nconst: end of the constant region */
	vir_block  *blocks;   uint32_t block_count;
	vir_slot *slot;  uint32_t slot_count;
	uint32_t *extra; uint32_t extra_count;
	vir_location  *locations;   uint32_t location_count;
	uint32_t *scratch;		/* per value: scratch for the running pass, undefined between passes */
	uint32_t valid;		/* VIR_VALID_*: the analyses whose results in blk[] are current (vir_analyses_invalidate) */
} vir_function;

/* ---- 9: signatures ---- */
enum { VIR_GROUP_NONE, VIR_GROUP_REG, VIR_GROUP_MEM, VIR_GROUP_SRET };
enum { VIR_CLASS_NONE, VIR_CLASS_INT, VIR_CLASS_F32, VIR_CLASS_F64 };	/* piece classes; 0 is an unused slot */
#define VIR_GROUP_KIND(w0)	((w0) & 15)
#define VIR_GROUP_PIECE_COUNT(w0)	(((w0) >> 4) & 15)
#define VIR_GROUP_CLASS(w0, i)	(((w0) >> (8 + 4 * (i))) & 15)
#define VIR_GROUP_FLAGS(w0)	((w0) >> 24)
#define VIR_GROUP_SIZE(w1)	((w1) & 0x0fffffffu)
#define VIR_GROUP_ALIGN_LOG2(w1)	((w1) >> 28)
#define VIR_GROUP_UNNAMED	1u	/* bit 0 of the flags: an unnamed (variadic) argument */
#define VIR_GROUP_RESULT	0xffffu	/* `param` group index of the result group (SRET pointer) */
#define VIR_SIGNATURE_HEADER(ngroups, variadic)	((uint32_t)(ngroups) | ((uint32_t)(variadic) << 16))
#define VIR_SIGNATURE_GROUP_COUNT(w)	((w) & 0xffffu)
#define VIR_SIGNATURE_IS_VARIADIC(w) (((w) >> 16) & 1)
/* word 0 of a group: kind, n pieces, classes c[0..3], flags */
#define VIR_GROUP_WORD0(kind, n, c0, c1, c2, c3, fl) \
	((uint32_t)(kind) | ((uint32_t)(n) << 4) | ((uint32_t)(c0) << 8) | ((uint32_t)(c1) << 12) | \
	 ((uint32_t)(c2) << 16) | ((uint32_t)(c3) << 20) | ((uint32_t)(fl) << 24))
#define VIR_GROUP_WORD1(size, lalign)	((uint32_t)(size) | ((uint32_t)(lalign) << 28))

/* ---- 18: limits ---- */
#define VIR_MAX_INS	((uint32_t)1 << 28)
#define VIR_MAX_BLK	((uint32_t)1 << 24)
#define VIR_MAX_SLOT	((uint32_t)1 << 24)
#define VIR_MAX_EXTRA	((uint32_t)1 << 28)
#define VIR_MAX_LOC	((uint32_t)1 << 26)
#define VIR_MAX_CONST	((uint32_t)1 << 23)	/* the constant table has 2^24 entries */
#define VIR_MAX_SWITCH	((uint32_t)1 << 20)
#define VIR_MAX_BPARAM	65535u			/* a block's parameters (vir_block.parameter_count is 16 bits) */

/* ---- the opcode table: what the printer, the parser and the verifier read ---- */
enum {	/* result type sets */
	VIR_TYPE_MASK_V = 1 << VIR_TYPE_VOID, VIR_TYPE_MASK_I32 = 1 << VIR_TYPE_I32, VIR_TYPE_MASK_I64 = 1 << VIR_TYPE_I64, VIR_TYPE_MASK_F32 = 1 << VIR_TYPE_F32,
	VIR_TYPE_MASK_F64 = 1 << VIR_TYPE_F64, VIR_TYPE_MASK_PTR = 1 << VIR_TYPE_PTR, VIR_TYPE_MASK_MULTI = 1 << VIR_TYPE_MULTI,
	VIR_TYPE_MASK_I = VIR_TYPE_MASK_I32 | VIR_TYPE_MASK_I64, VIR_TYPE_MASK_F = VIR_TYPE_MASK_F32 | VIR_TYPE_MASK_F64, VIR_TYPE_MASK_ANY = VIR_TYPE_MASK_I | VIR_TYPE_MASK_F | VIR_TYPE_MASK_PTR,
	VIR_TYPE_MASK_F32X4 = 1 << VIR_TYPE_F32X4, VIR_TYPE_MASK_I32X4 = 1 << VIR_TYPE_I32X4, VIR_TYPE_MASK_F64X2 = 1 << VIR_TYPE_F64X2, VIR_TYPE_MASK_I64X2 = 1 << VIR_TYPE_I64X2,
	VIR_TYPE_MASK_VI = VIR_TYPE_MASK_I32X4 | VIR_TYPE_MASK_I64X2, VIR_TYPE_MASK_VF = VIR_TYPE_MASK_F32X4 | VIR_TYPE_MASK_F64X2, VIR_TYPE_MASK_VEC = VIR_TYPE_MASK_VI | VIR_TYPE_MASK_VF
};
enum {	/* operand kinds (a, b, c) */
	VIR_OPERAND_KIND_NONE,	/* must be 0 */
	VIR_OPERAND_KIND_T,		/* a value of the result's type */
	VIR_OPERAND_KIND_I,		/* a value, i32 or i64 */
	VIR_OPERAND_KIND_I32, VIR_OPERAND_KIND_I64, VIR_OPERAND_KIND_PTR, VIR_OPERAND_KIND_F32, VIR_OPERAND_KIND_F64,
	VIR_OPERAND_KIND_F,		/* a value, f32 or f64 */
	VIR_OPERAND_KIND_ANY,		/* a value of any type but void and multi */
	VIR_OPERAND_KIND_X,		/* icmp: i32, i64 or ptr, both the same */
	VIR_OPERAND_KIND_FP,		/* fcmp: f32 or f64, both the same */
	VIR_OPERAND_KIND_VI,		/* the integer vector with the result's lane width (a mask; vs2f's operand) */
	VIR_OPERAND_KIND_VF,		/* the float vector with the result's lane width (vfcmp) */
	VIR_OPERAND_KIND_ELEM,	/* a value of the result's lane type (vsplat) */
	VIR_OPERAND_KIND_VRED,	/* the integer vector whose lanes have the result's type (vreduce) */
	VIR_OPERAND_KIND_VLANES,	/* vector whose element type is the scalar result */
	VIR_OPERAND_KIND_MULTI,	/* a value of type multi (proj) */
	VIR_OPERAND_KIND_BITC,	/* bitcast: i32 for f32, f32 for i32, i64 for f64, f64 for i64 */
	VIR_OPERAND_KIND_IMM,		/* an immediate */
	VIR_OPERAND_KIND_BLK,		/* a block id */
	VIR_OPERAND_KIND_EXT,		/* an extra offset */
	VIR_OPERAND_KIND_SIG,		/* an extra offset of a signature (call) */
	VIR_OPERAND_KIND_GRP,		/* an extra offset of one argument group (va_arg) */
	VIR_OPERAND_KIND_TPL,		/* an asm template id */
	VIR_OPERAND_KIND_SYMREF,	/* a unit symbol (tlsaddr) */
	VIR_OPERAND_KIND_CONSTV	/* a value that must be an iconst (memcpy, memset size) */
};
enum {	/* effects, 7.3 */
	VIR_EFFECT_P = 1, VIR_EFFECT_R = 2, VIR_EFFECT_W = 4, VIR_EFFECT_U = 8, VIR_EFFECT_C = 16, VIR_EFFECT_T = 32, VIR_EFFECT_A = 64, VIR_EFFECT_F = 128,
	VIR_EFFECT_UF = 256		/* U only when a UB flag is set (U*) */
};
enum {	/* text forms, 11.4 */
	VIR_FORM_STD, VIR_FORM_NONE, VIR_FORM_CC, VIR_FORM_EXT, VIR_FORM_LOAD, VIR_FORM_STORE, VIR_FORM_ATOM, VIR_FORM_MEMCPY, VIR_FORM_PARAM, VIR_FORM_CALL,
	VIR_FORM_VAARG, VIR_FORM_ASM, VIR_FORM_ASMGOTO, VIR_FORM_BR, VIR_FORM_SWITCH, VIR_FORM_IBR, VIR_FORM_RET, VIR_FORM_CONST, VIR_FORM_BPARAM, VIR_FORM_TLS
};
typedef struct vir_opcode_info {
	const char *name;	/* NULL: not an opcode */
	uint8_t op, form;
	uint16_t type_mask;		/* allowed result types (TM_*) */
	uint8_t operand_kinds[3];		/* operand kinds of a, b, c */
	uint16_t flags_mask;	/* allowed flag bits (7.2); more bits must be 0 */
	uint16_t effects;		/* EF_* */
} vir_opcode_info;
extern const vir_opcode_info vir_opcode_table[256];
const vir_opcode_info *vir_opcode_find(const char *s, size_t n);	/* by name, or NULL */

extern const char *const vir_type_names[VIR_TYPE_COUNT];
extern const char *const vir_integer_condition_names[10], *const vir_float_condition_names[8];
bool vir_opcode_is_constant(uint32_t op);
bool vir_opcode_is_terminator(uint32_t op);
/* the type the operand position fixes, or VIR_TYPE_VOID when the text needs a `:type` suffix (11.5) */
uint32_t vir_operand_type_fixed(uint32_t op, uint32_t ty, uint32_t k);
uint32_t vir_type_size(uint32_t ty);	/* bytes of a scalar type, 0 otherwise */

/* ---- the thread's memory (22.2, 22.3) ---- */
typedef struct vir_context vir_context;
vir_context *vir_context_get(void);
bool vir_is_failed(void);		/* a limit (section 18) was exceeded; cleared by vir_function_begin */

/* ---- construction ---- */
void vir_function_begin(vir_function *f, uint32_t target, uint32_t sym, uint32_t fl);
uint32_t vir_constant_add(vir_function *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c);	/* hash-consed; 0 on failure */
uint32_t vir_constant_raw_add(vir_function *f, uint32_t op, uint32_t ty, uint32_t a, uint32_t b, uint32_t c);	/* not unified (tests) */
uint32_t vir_block_add(vir_function *f, uint16_t fl);
uint32_t vir_insn_add(vir_function *f, uint32_t op, uint32_t ty, uint32_t fl, uint32_t a, uint32_t b, uint32_t c);
uint32_t vir_slot_add(vir_function *f, uint64_t size, uint32_t align, uint32_t fl, int64_t hint);
uint32_t vir_extra_add(vir_function *f, const uint32_t *w, uint32_t n);	/* offset of a copy of w[0..n); 0 on failure */
void vir_location_add(vir_function *f, uint32_t loc);	/* a run starting at the next instruction */

/* ---- analyses and the canonical form ---- */
bool vir_cfg_build(vir_function *f);	/* blocks[].predecessor_first/predecessor_count (computed once until invalidated); false if a terminator's target is out of range */
bool vir_dominators_build(vir_function *f);	/* blocks[].immediate_dominator/preorder/preorder_last (Cooper-Harvey-Kennedy), computed once until invalidated; needs vir_cfg_build */
static inline void vir_analyses_invalidate(vir_function *f)	/* blocks, edges or terminators changed: every analysis is stale */
{
	f->valid = 0;
}
static inline bool vir_block_dominates(const vir_function *f, uint32_t a, uint32_t b)	/* a dominates b (reflexive) */
{
	return f->blocks[a].preorder <= f->blocks[b].preorder && f->blocks[b].preorder <= f->blocks[a].preorder_last;
}
/* low32 has nins bytes: nonzero if every use needs only the low 32 bits.
 * ret32: the target ABI ignores the integer return value's high half. */
void vir_demand_low32_compute(vir_function *f, bool ret32, uint8_t *low32);
uint32_t vir_value_possible_bits(const vir_function *f, uint32_t v, unsigned depth); /* possible one bits; UINT32_MAX when unknown */
/* Read exactly 2/4/8 big-endian bytes; private consecutive expression in [first, limit). */
unsigned vir_big_endian_read_match(const vir_function *f, const uint8_t *uses, uint32_t first, uint32_t limit, uint32_t *last);
/* v is shl.nsw.i32 followed by its signed widening; widening may precede scaling. */
bool vir_sign_extend_shift_match(const vir_function *f, uint32_t v, unsigned *count);
void vir_uses_count(const vir_function *f, uint8_t *cnt);	/* use count per value, saturating at 255; cnt has nins bytes */
bool vir_canon(vir_function *f);	/* 2.4; false (function unchanged) when it exceeds a limit or its graph is malformed */

/* iterating the value operands and the block references of an instruction */
typedef struct vir_operand_iterator {
	vir_function *function;
	vir_insn *insn;
	uint32_t field_mask, field_index;	/* fields a, b, c still to yield (bit mask), the next one */
	uint32_t base, count, stride, index;	/* the extra words: base + i * stride, for i < n */
	uint32_t pos;		/* 0 a, 1 b, 2 c, 3 an extra word: the word last returned */
} vir_operand_iterator;
/* The state is read when the iterator is made, so the instruction's fields
 * may be rewritten while it runs.  Values mode: every operand that is a
 * value (including the lists of call, br, ret, asm); blocks mode: every
 * block id (successors of a terminator, in operand order; labeladdr's) */
void vir_operand_iterator_init(vir_operand_iterator *it, vir_function *f, vir_insn *in, bool blocks);
uint32_t *vir_operand_iterator_next(vir_operand_iterator *it);	/* the next word (a value index or a block id) to read or rewrite; NULL at the end */

/* ---- units: the text form's world (2.5) ---- */
enum { VIR_UNIT_ITEM_EXTERN, VIR_UNIT_ITEM_DATA, VIR_UNIT_ITEM_TDATA, VIR_UNIT_ITEM_FUNC, VIR_UNIT_ITEM_UNDECL };
typedef struct vir_item {	/* a data item: `i32 5`, `zero 8`, `bytes "..."`, `ptr @x+4` */
	uint8_t type;		/* VT_*, or 0xff zero, 0xfe bytes */
	uint32_t symbol;		/* ptr item: the symbol + 1, or 0 */
	uint64_t value;		/* the value, the addend, the zero count, the byte count */
	const char *bytes;
} vir_item;
typedef struct vir_unit_symbol {
	const char *name;
	uint8_t kind;
	uint32_t align, item_count, item_first;	/* data: align, items [item0, item0+nitem) */
	uint32_t line, column;		/* first mention (for errors) */
} vir_unit_symbol;
typedef struct vir_unit_signature { uint32_t offset, count; } vir_unit_signature;	/* words [off, off+n) of unit->sigw */
typedef struct vir_unit_asm {
	const char *template;
	uint32_t output_count, input_count, clobber_count, labels;
	const char **outputs, **inputs, **clobbers;
} vir_unit_asm;
typedef struct vir_unit {
	uint32_t version, target;
	vir_unit_symbol *symbols;   uint32_t symbol_count, symbol_capacity;
	vir_unit_signature *signatures;   uint32_t signature_count, signature_capacity;
	uint32_t *signature_words;  uint32_t signature_word_count, signature_word_capacity;
	vir_unit_asm *asms;    uint32_t asm_count, asm_capacity;
	vir_item *item;  uint32_t item_count, item_capacity;
	vir_function   *functions;    uint32_t function_count, function_capacity;	/* saved copies; vir_function_load puts one into the thread's context */
	uint32_t *function_symbols;			/* per function: its unit symbol */
	void **heap; uint32_t heap_size, heap_capacity;	/* what the unit owns, freed by vir_unit_free */
} vir_unit;
void vir_unit_init(vir_unit *u);
void vir_unit_free(vir_unit *u);
void vir_function_save(vir_unit *u, const vir_function *f, uint32_t sym);	/* appends a malloc'ed copy */
void vir_function_load(vir_function *f, const vir_unit *u, uint32_t i);	/* into the thread's context (invalidates the current function) */
void vir_function_store(vir_unit *u, uint32_t i, const vir_function *f);	/* replace copy i */
uint32_t vir_unit_symbol_intern(vir_unit *u, const char *name, size_t n);	/* find or add (kind VIR_UNIT_ITEM_UNDECL) */
uint32_t vir_unit_signature_words_intern(vir_unit *u, const uint32_t *w, uint32_t n);	/* find or add; the index */

/* ---- text form (11) ---- */
typedef struct vir_parse_error { uint32_t line, col; char message[200]; } vir_parse_error;
bool vir_parse(vir_unit *u, const char *src, size_t n, vir_parse_error *err);	/* false: err says where */
void vir_print_function(vbuffer *b, const vir_unit *u, const vir_function *f);
void vir_print_unit(vbuffer *b, const vir_unit *u);	/* the functions through vir_function_load, one at a time */
const char *vir_target_name(uint32_t t);
void vir_print_signature(vbuffer *b, const uint32_t *w);	/* `(int, f64, ...) -> int` for the signature words w */
void vir_print_group(vbuffer *b, uint32_t w0, uint32_t w1);

/* ---- verifier (12) ---- */
/* checks every rule of section 12; each failure is appended to out as
 * `@func bN.I: RULE: message` (the first max); returns the number of failures */
uint32_t vir_verify(vir_function *f, const vir_unit *u, vbuffer *out, uint32_t max);
const char *vir_function_name(const vir_unit *u, const vir_function *f);	/* the name of f's symbol, or "?" */

#endif
