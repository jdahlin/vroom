#ifndef VCC_A64_H
#define VCC_A64_H
/*
 * A64 instruction encoder.
 *
 * Two layers:
 *  - static inline encoders for the common instruction classes, for the
 *    emitter (pure functions, no state, no allocation);
 *  - a64_asm(): the assembler's entry point, a mnemonic table over parsed
 *    operands (A64Op), producing one instruction word and at most one
 *    fixup (an R_AARCH64_* type and a symbolic value for elf_fixup()).
 *
 * Everything here is reentrant; the tables are const.
 */
#include <stddef.h>
#include <stdint.h>
#include "../../obj/obj.h"

/* ---- registers ---- */
enum { A64_X, A64_W, A64_V, A64_Q, A64_D, A64_S, A64_H, A64_B, A64_Z, A64_P };	/* A64Op.cls (Z/P: SVE) */
#define A64_ZR 31	/* xzr/wzr (A64Op.sp == 0) */
#define A64_SP 31	/* sp/wsp  (A64Op.sp == 1) */

/* vector arrangements (A64Op.arr): size + Q */
enum { A64_ARR_NONE, A64_8B, A64_16B, A64_4H, A64_8H, A64_2S, A64_4S, A64_1D, A64_2D,
       A64_1Q, A64_ELB, A64_ELH, A64_ELS, A64_ELD, A64_ELQ /* element only: .b .h .s .d .q */ };

/* shifts / extends (A64Op.shift) */
enum { A64_LSL, A64_LSR, A64_ASR, A64_ROR, A64_MSL,
       A64_UXTB, A64_UXTH, A64_UXTW, A64_UXTX, A64_SXTB, A64_SXTH, A64_SXTW, A64_SXTX,
       A64_MUL /* SVE: mul #imm */ };

/* condition codes */
enum { A64_EQ, A64_NE, A64_HS, A64_LO, A64_MI, A64_PL, A64_VS, A64_VC,
       A64_HI, A64_LS, A64_GE, A64_LT, A64_GT, A64_LE, A64_AL, A64_NV };

/* relocation-operator modifiers (A64Op.mod) */
enum { A64_MOD_NONE, A64_MOD_LO12, A64_MOD_PG_HI21, A64_MOD_PG_HI21_NC,
       A64_MOD_ABS_G0, A64_MOD_ABS_G0_NC, A64_MOD_ABS_G1, A64_MOD_ABS_G1_NC,
       A64_MOD_ABS_G2, A64_MOD_ABS_G2_NC, A64_MOD_ABS_G3, A64_MOD_GOT, A64_MOD_GOT_LO12,
       A64_MOD_ABS_G0_S, A64_MOD_ABS_G1_S, A64_MOD_ABS_G2_S,
       /* TLS and pc-relative operators */
       A64_MOD_TLSDESC, A64_MOD_TLSDESC_LO12, A64_MOD_GOTTPREL, A64_MOD_GOTTPREL_LO12_NC,
       A64_MOD_GOTTPREL_G1, A64_MOD_GOTTPREL_G0_NC,
       A64_MOD_TPREL_G2, A64_MOD_TPREL_G1, A64_MOD_TPREL_G1_NC, A64_MOD_TPREL_G0, A64_MOD_TPREL_G0_NC,
       A64_MOD_TPREL_HI12, A64_MOD_TPREL_LO12, A64_MOD_TPREL_LO12_NC,
       A64_MOD_DTPREL_G2, A64_MOD_DTPREL_G1, A64_MOD_DTPREL_G1_NC, A64_MOD_DTPREL_G0, A64_MOD_DTPREL_G0_NC,
       A64_MOD_DTPREL_HI12, A64_MOD_DTPREL_LO12, A64_MOD_DTPREL_LO12_NC,
       A64_MOD_PREL_G3, A64_MOD_PREL_G2, A64_MOD_PREL_G2_NC, A64_MOD_PREL_G1, A64_MOD_PREL_G1_NC,
       A64_MOD_PREL_G0, A64_MOD_PREL_G0_NC,
       /* Mach-O x@TLVPPAGE / x@TLVPPAGEOFF (as_darwin_text spells them :vcc_tlvp:) */
       A64_MOD_TLVP, A64_MOD_TLVP_LO12, A64_MOD_COUNT };

/* ---- parsed operand ---- */
enum {
	A64O_NONE, A64O_REG, A64O_VREG, A64O_VELEM, A64O_LIST, A64O_IMM,
	A64O_MEM, A64O_SHIFT, A64O_COND, A64O_NAME, A64O_FIMM,
};
enum { A64M_OFF, A64M_PRE, A64M_POST, A64M_REG };	/* A64Op.mode */

typedef struct {
	uint8_t kind;
	uint8_t cls;		/* REG/VREG/VELEM/LIST: register class */
	uint8_t reg;		/* register number; MEM: base */
	uint8_t sp;		/* REG: written as sp/wsp; MEM: unused */
	uint8_t arr;		/* VREG/VELEM/LIST arrangement */
	uint8_t nregs;		/* LIST: count */
	uint8_t idx;		/* VELEM/LIST: element index */
	uint8_t hasidx;
	uint8_t shift;		/* SHIFT: kind; MEM REG mode: extend kind */
	uint8_t hasamt;		/* SHIFT/MEM: an explicit #amount was given */
	uint8_t mode;		/* MEM: A64M_* */
	uint8_t index;		/* MEM REG mode: index register */
	uint8_t idxcls;		/* MEM REG mode: A64_X / A64_W */
	uint8_t mod;		/* IMM/MEM offset: A64_MOD_* */
	uint8_t hassym;		/* IMM/MEM offset: v has symbols (not a constant) */
	uint8_t hash;		/* IMM: written with '#' */
	uint8_t wb;		/* REG: followed by '!' (MOPS) */
	uint8_t mulvl;		/* MEM: offset followed by ', mul vl' (SVE) */
	uint8_t pq;		/* REG P: /z = 1, /m = 2 (SVE predication) */
	int64_t imm;		/* IMM value, MEM offset, SHIFT amount */
	double fimm;		/* FIMM */
	ElfVal v;		/* symbolic part (v.c holds the constant part) */
	const char *name;	/* NAME / COND spelling */
	uint32_t nlen;
} A64Op;

typedef struct { uint32_t type; ElfVal v; } A64Fix;	/* type 0 = no fixup */

/* Assemble one instruction.  Returns 0 and sets *out (and *fix) or returns
 * -1 with *err pointing at a static message. */
/* SVE predicate pattern (pow2, vl1 ... all) -> its number, else -1 */
int a64_svepat(const char *s, size_t n);
int a64_asm(const char *mn, size_t mnlen, const A64Op *op, int nop,
	    uint32_t *out, A64Fix *fix, const char **err);

/* System register / PSTATE / operation-name lookups (generated tables). */
int a64_sysreg(const char *name, size_t len, int write);	/* op0:op1:CRn:CRm:op2 (16 bits) or -1 */
int a64_cond(const char *name, size_t len);			/* -1 if not a condition */

/* Logical (bitmask) immediate: N:immr:imms (13 bits) or -1. */
int a64_logimm(uint64_t v, int is64);

/* ---- inline encoders for the emitter ---- */
static inline uint32_t a64_movz(int sf, unsigned rd, unsigned imm16, unsigned hw)
{ return 0x52800000u | (uint32_t)sf << 31 | hw << 21 | (imm16 & 0xffff) << 5 | rd; }
static inline uint32_t a64_movn(int sf, unsigned rd, unsigned imm16, unsigned hw)
{ return 0x12800000u | (uint32_t)sf << 31 | hw << 21 | (imm16 & 0xffff) << 5 | rd; }
static inline uint32_t a64_movk(int sf, unsigned rd, unsigned imm16, unsigned hw)
{ return 0x72800000u | (uint32_t)sf << 31 | hw << 21 | (imm16 & 0xffff) << 5 | rd; }
/* add/sub (immediate): op 0 add 1 sub, s sets flags; rn/rd 31 = sp (rd: zr if s) */
static inline uint32_t a64_addsub_imm(int sf, int op, int s, unsigned rd, unsigned rn, unsigned imm12, int lsl12)
{ return 0x11000000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | (uint32_t)lsl12 << 22 | (imm12 & 0xfff) << 10 | rn << 5 | rd; }
/* add/sub (shifted register) */
static inline uint32_t a64_addsub_reg(int sf, int op, int s, unsigned rd, unsigned rn, unsigned rm, unsigned shift, unsigned amt)
{ return 0x0b000000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | shift << 22 | rm << 16 | (amt & 63) << 10 | rn << 5 | rd; }
/* logical (shifted register): opc 0 and 1 orr 2 eor 3 ands; n inverts rm */
static inline uint32_t a64_logic_reg(int sf, unsigned opc, int n, unsigned rd, unsigned rn, unsigned rm, unsigned shift, unsigned amt)
{ return 0x0a000000u | (uint32_t)sf << 31 | opc << 29 | shift << 22 | (uint32_t)n << 21 | rm << 16 | (amt & 63) << 10 | rn << 5 | rd; }
/* logical (immediate): nimms from a64_logimm() */
static inline uint32_t a64_logic_imm(int sf, unsigned opc, unsigned rd, unsigned rn, unsigned nimms)
{ return 0x12000000u | (uint32_t)sf << 31 | opc << 29 | nimms << 10 | rn << 5 | rd; }
/* bitfield: opc 0 sbfm 1 bfm 2 ubfm */
static inline uint32_t a64_bfm(int sf, unsigned opc, unsigned rd, unsigned rn, unsigned immr, unsigned imms)
{ return 0x13000000u | (uint32_t)sf << 31 | opc << 29 | (uint32_t)sf << 22 | immr << 16 | imms << 10 | rn << 5 | rd; }
/* data processing 3-source: madd (o0=0) / msub (o0=1) */
static inline uint32_t a64_madd(int sf, int o0, unsigned rd, unsigned rn, unsigned rm, unsigned ra)
{ return 0x1b000000u | (uint32_t)sf << 31 | rm << 16 | (uint32_t)o0 << 15 | ra << 10 | rn << 5 | rd; }
/* data processing 2-source: opcode 2 udiv 3 sdiv 8 lslv 9 lsrv 10 asrv 11 rorv */
static inline uint32_t a64_dp2(int sf, unsigned opcode, unsigned rd, unsigned rn, unsigned rm)
{ return 0x1ac00000u | (uint32_t)sf << 31 | rm << 16 | opcode << 10 | rn << 5 | rd; }
/* conditional select: op/o2 0,0 csel 0,1 csinc 1,0 csinv 1,1 csneg */
static inline uint32_t a64_csel(int sf, int op, int o2, unsigned rd, unsigned rn, unsigned rm, unsigned cond)
{ return 0x1a800000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | rm << 16 | cond << 12 | (uint32_t)o2 << 10 | rn << 5 | rd; }
/* load/store unsigned offset: size 0..3, opc 0 str 1 ldr 2 ldrs(64) 3 ldrs(32); v = SIMD&FP */
static inline uint32_t a64_ldst_uoff(unsigned size, int v, unsigned opc, unsigned rt, unsigned rn, unsigned uimm12)
{ return 0x39000000u | size << 30 | (uint32_t)v << 26 | opc << 22 | (uimm12 & 0xfff) << 10 | rn << 5 | rt; }
/* load/store unscaled / pre / post: idx 0 unscaled(ldur) 1 post 3 pre */
static inline uint32_t a64_ldst_imm9(unsigned size, int v, unsigned opc, unsigned idx, unsigned rt, unsigned rn, int simm9)
{ return 0x38000000u | size << 30 | (uint32_t)v << 26 | opc << 22 | ((uint32_t)simm9 & 0x1ff) << 12 | idx << 10 | rn << 5 | rt; }
/* load/store pair: opc 0 32-bit 2 64-bit; mode 1 post 2 offset 3 pre; l load */
static inline uint32_t a64_ldstp(unsigned opc, int v, unsigned mode, int l, unsigned rt, unsigned rt2, unsigned rn, int simm7)
{ return 0x28000000u | opc << 30 | (uint32_t)v << 26 | mode << 23 | (uint32_t)l << 22 | ((uint32_t)simm7 & 0x7f) << 15 | rt2 << 10 | rn << 5 | rt; }
static inline uint32_t a64_b(int64_t off) { return 0x14000000u | ((uint32_t)(off >> 2) & 0x3ffffff); }
static inline uint32_t a64_bl(int64_t off) { return 0x94000000u | ((uint32_t)(off >> 2) & 0x3ffffff); }
static inline uint32_t a64_bcond(unsigned cond, int64_t off) { return 0x54000000u | ((uint32_t)(off >> 2) & 0x7ffff) << 5 | cond; }
static inline uint32_t a64_cbz(int sf, int nz, unsigned rt, int64_t off)
{ return 0x34000000u | (uint32_t)sf << 31 | (uint32_t)nz << 24 | ((uint32_t)(off >> 2) & 0x7ffff) << 5 | rt; }
static inline uint32_t a64_tbz(int nz, unsigned rt, unsigned bit, int64_t off)
{ return 0x36000000u | (bit >> 5) << 31 | (uint32_t)nz << 24 | (bit & 31) << 19 | ((uint32_t)(off >> 2) & 0x3fff) << 5 | rt; }
static inline uint32_t a64_br(unsigned rn) { return 0xd61f0000u | rn << 5; }
static inline uint32_t a64_blr(unsigned rn) { return 0xd63f0000u | rn << 5; }
static inline uint32_t a64_ret(unsigned rn) { return 0xd65f0000u | rn << 5; }
static inline uint32_t a64_adr(unsigned rd, int64_t off)
{ return 0x10000000u | ((uint32_t)off & 3) << 29 | ((uint32_t)(off >> 2) & 0x7ffff) << 5 | rd; }
static inline uint32_t a64_adrp(unsigned rd, int64_t pages)
{ return 0x90000000u | ((uint32_t)pages & 3) << 29 | ((uint32_t)(pages >> 2) & 0x7ffff) << 5 | rd; }
#define A64_NOP 0xd503201fu

#endif
