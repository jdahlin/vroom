/*
 * a64.c: A64 encoder for the assembler, over parsed operands (a64.h).
 *
 * A sorted mnemonic table maps each mnemonic to an encoder class and its
 * base opcode; the class function checks the operand shapes and fills the
 * fields.  Aliases (mov, cmp, tst, lsl, cset, ...) map to their canonical
 * instruction the way LLVM's AArch64AsmParser chooses them, so the
 * encodings are the ones clang produces.
 */
#include "../a64.h"
#include <string.h>
#include <strings.h>
#include <pthread.h>

typedef struct { const char *name; uint16_t enc; uint8_t rw; } SysReg;
static const SysReg sysregs[] = {
#include "../a64sysreg.inc"
};

static int lc_cmp(const char *a, size_t n, const char *b)
{
	for (size_t i = 0; i < n; i++) {
		int ca = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i];
		int cb = (unsigned char)b[i];
		if (!cb) return 1;
		if (ca != cb) return ca - cb;
	}
	return b[n] ? -1 : 0;
}

static int cinum(const char *s, size_t n, int max)
{
	if (!n || n > 2) return -1;
	int v = 0;
	for (size_t i = 0; i < n; i++) { if (s[i] < '0' || s[i] > '9') return -1; v = v * 10 + s[i] - '0'; }
	return v <= max ? v : -1;
}

/* case-insensitive FNV-1a, for the lookup tables built once below */
static uint32_t lc_hash(const char *s, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++) h = (h ^ ((uint8_t)s[i] | 32)) * 16777619u;
	return h ^ h >> 15;
}

#define SRH 4096
static uint16_t srh[SRH];	/* sysregs index + 1, 0 = empty */
static pthread_once_t srh_once = PTHREAD_ONCE_INIT;

static void srh_build(void)
{
	for (size_t i = 0; i < sizeof sysregs / sizeof sysregs[0]; i++) {
		uint32_t j = lc_hash(sysregs[i].name, strlen(sysregs[i].name)) & (SRH - 1);
		while (srh[j]) j = (j + 1) & (SRH - 1);
		srh[j] = (uint16_t)(i + 1);
	}
}

int a64_sysreg(const char *name, size_t len, int write)
{
	(void)write;
	pthread_once(&srh_once, srh_build);
	for (uint32_t j = lc_hash(name, len) & (SRH - 1); srh[j]; j = (j + 1) & (SRH - 1)) {
		const SysReg *r = &sysregs[srh[j] - 1];
		if (!lc_cmp(name, len, r->name)) return r->enc;
	}
	/* generic S<op0>_<op1>_C<n>_C<m>_<op2> */
	if (len >= 11 && (name[0] == 's' || name[0] == 'S')) {
		int f[5], k = 0;
		const char *p = name + 1, *e = name + len;
		while (p < e && k < 5) {
			if (k == 2 || k == 3) { if (*p != 'c' && *p != 'C') return -1; p++; }
			const char *s = p;
			while (p < e && *p != '_') p++;
			f[k] = cinum(s, (size_t)(p - s), 15);
			if (f[k] < 0) return -1;
			k++;
			if (p < e) p++;
		}
		if (k != 5 || f[0] > 3 || f[1] > 7 || f[4] > 7) return -1;
		return f[0] << 14 | f[1] << 11 | f[2] << 7 | f[3] << 3 | f[4];
	}
	return -1;
}

static const char *conds[] = { "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al", "nv" };

int a64_cond(const char *s, size_t n)
{
	if (n != 2) return -1;
	char a = (char)(s[0] | 32), b = (char)(s[1] | 32);
	for (int i = 0; i < 16; i++) if (conds[i][0] == a && conds[i][1] == b) return i;
	if (a == 'c' && b == 's') return A64_HS;
	if (a == 'c' && b == 'c') return A64_LO;
	return -1;
}

/* bitmask immediate: LLVM's processLogicalImmediate */
static int shifted_mask(uint64_t v) { return v && (((v | (v - 1)) + 1) & (v | (v - 1))) == 0; }

int a64_logimm(uint64_t imm, int is64)
{
	unsigned regsize = is64 ? 64 : 32;
	if (imm == 0 || imm == ~0ull || (regsize != 64 && (imm >> regsize || imm == (~0ull >> (64 - regsize)))))
		return -1;
	unsigned size = regsize;
	do {
		size /= 2;
		uint64_t mask = (1ull << size) - 1;
		if ((imm & mask) != ((imm >> size) & mask)) { size *= 2; break; }
	} while (size > 2);
	uint64_t mask = ~0ull >> (64 - size);
	unsigned cto, i;
	imm &= mask;
	if (shifted_mask(imm)) {
		i = (unsigned)__builtin_ctzll(imm);
		cto = (unsigned)__builtin_ctzll(~(imm >> i));
	} else {
		imm |= ~mask;
		if (!shifted_mask(~imm)) return -1;
		unsigned clo = (unsigned)__builtin_clzll(~imm);
		i = 64 - clo;
		cto = clo + (unsigned)__builtin_ctzll(~imm) - (64 - size);
	}
	unsigned immr = (size - i) & (size - 1);
	uint64_t nimms = ~(uint64_t)(size - 1) << 1;
	nimms |= cto - 1;
	unsigned n = ((nimms >> 6) & 1) ^ 1;
	return (int)(n << 12 | immr << 6 | (nimms & 0x3f));
}

/* ---------------------------------------------------------------- helpers */

typedef struct {
	const A64Op *op; int n;
	uint32_t *out; A64Fix *fix; const char **err;
	const char *mn;
} Ctx;

#define FAIL(msg) do { *c->err = (msg); return -1; } while (0)
#define BADOPS FAIL("invalid operand for instruction")

static int isreg(const A64Op *o) { return o->kind == A64O_REG; }
static int isgpr(const A64Op *o) { return o->kind == A64O_REG && (o->cls == A64_X || o->cls == A64_W); }
static int isx(const A64Op *o) { return o->kind == A64O_REG && o->cls == A64_X; }
static int isw(const A64Op *o) { return o->kind == A64O_REG && o->cls == A64_W; }
static int issp(const A64Op *o) { return isgpr(o) && o->sp; }
static int iszr(const A64Op *o) { return isgpr(o) && o->reg == 31 && !o->sp; }
static int isimm(const A64Op *o) { return o->kind == A64O_IMM; }
static int iscimm(const A64Op *o) { return o->kind == A64O_IMM && !o->hassym && !o->mod; }
static unsigned R(const A64Op *o) { return o->reg; }

static int setfix(Ctx *c, uint32_t type, const A64Op *o)
{
	c->fix->type = type;
	c->fix->v = o->v;
	return 0;
}

/* register-number checks: no SP where ZR is meant and vice versa */
static int gpr_nosp(const A64Op *o) { return isgpr(o) && !o->sp; }

static unsigned arr_q(unsigned arr) { return arr == A64_16B || arr == A64_8H || arr == A64_4S || arr == A64_2D || arr == A64_1Q; }
static int arr_size(unsigned arr)
{
	switch (arr) {
	case A64_8B: case A64_16B: case A64_ELB: return 0;
	case A64_4H: case A64_8H: case A64_ELH: return 1;
	case A64_2S: case A64_4S: case A64_ELS: return 2;
	case A64_1D: case A64_2D: case A64_ELD: return 3;
	case A64_1Q: case A64_ELQ: return 4;
	}
	return -1;
}
static int arr_bits(unsigned arr) { return arr_q(arr) ? 128 : 64; }

/* ---------------------------------------------------------------- classes */

enum {
	K_ADDSUB, K_CMP, K_NEG, K_LOGIC, K_TST, K_MOV, K_MOVW, K_SHIFT, K_BFX, K_BFM, K_BFI,
	K_ADR, K_B, K_BCOND, K_CBZ, K_TBZ, K_BR, K_RET, K_LDST, K_LDSTU, K_LDSTT, K_LDP,
	K_LDX, K_STX, K_PRFM, K_FIXED, K_HINT, K_BTI, K_EXC, K_BARRIER, K_MSR, K_MRS,
	K_DC, K_IC, K_TLBI, K_SYS, K_CSEL, K_CSET, K_CNEG, K_CCMP, K_DP1, K_MUL, K_CRC,
	K_DP2, K_EXTR, K_VLD1, K_VREV, K_DUP, K_EXT, K_MOVI, K_VSHIFT, K_VPERM, K_PMULL,
	K_FMOV, K_SHA3, K_SHA2, K_CPY, K_SET, K_V3, K_UMOV, K_AT, K_RDVL, K_PREG,
	K_V2, K_TBL, K_SHA512, K_LDR_REP, K_INS, K_MTE, K_MTEM, K_VNOT, K_SMOV, K_ACROSS,
	K_VMISC, K_ADDP, K_IALIAS, K_PAC, K_FP,
};

typedef struct { const char *name; uint8_t k; uint32_t base; uint32_t x; } Mn;

/* sorted by name */
static const Mn mns[] = {
	{ "abs", K_IALIAS, 0x5ac02000, 3 },
	{ "adc", K_DP2, 0x1a000000, 0 },
	{ "adcs", K_DP2, 0x3a000000, 0 },
	{ "add", K_ADDSUB, 0, 0 },
	{ "addg", K_PAC, 0x91800000, 17 },
	{ "addp", K_ADDP, 0x0e20bc00, 0 },
	{ "adds", K_ADDSUB, 1, 0 },
	{ "addv", K_ACROSS, 0x0e31b800, 0 },
	{ "adr", K_ADR, 0x10000000, 0 },
	{ "adrp", K_ADR, 0x90000000, 1 },
	{ "aesd", K_V2, 0x4e285800, 0 },
	{ "aese", K_V2, 0x4e284800, 0 },
	{ "aesimc", K_V2, 0x4e287800, 0 },
	{ "aesmc", K_V2, 0x4e286800, 0 },
	{ "and", K_LOGIC, 0, 0 },
	{ "ands", K_LOGIC, 3, 0 },
	{ "asr", K_SHIFT, 2, 0 },
	{ "at", K_AT, 0, 0 },
	{ "autda", K_PAC, 0xdac11800, 3 },
	{ "autdb", K_PAC, 0xdac11c00, 3 },
	{ "autdza", K_PAC, 0xdac13be0, 1 },
	{ "autdzb", K_PAC, 0xdac13fe0, 1 },
	{ "autia", K_PAC, 0xdac11000, 3 },
	{ "autia1716", K_FIXED, 0xd503219f, 0 },
	{ "autiasp", K_FIXED, 0xd50323bf, 0 },
	{ "autiaz", K_FIXED, 0xd503239f, 0 },
	{ "autib", K_PAC, 0xdac11400, 3 },
	{ "autib1716", K_FIXED, 0xd50321df, 0 },
	{ "autibsp", K_FIXED, 0xd50323ff, 0 },
	{ "autibz", K_FIXED, 0xd50323df, 0 },
	{ "autiza", K_PAC, 0xdac133e0, 1 },
	{ "autizb", K_PAC, 0xdac137e0, 1 },
	{ "b", K_B, 0x14000000, R_AARCH64_JUMP26 },
	{ "bcax", K_SHA512, 0xce200000, 3 },
	{ "bfc", K_IALIAS, 0, 5 },
	{ "bfi", K_BFI, 1, 0 },
	{ "bfm", K_BFM, 1, 0 },
	{ "bfxil", K_BFX, 1, 0 },
	{ "bic", K_LOGIC, 0, 1 },
	{ "bics", K_LOGIC, 3, 1 },
	{ "bif", K_V3, 0x2ee01c00, 0 },
	{ "bit", K_V3, 0x2ea01c00, 0 },
	{ "bl", K_B, 0x94000000, R_AARCH64_CALL26 },
	{ "blr", K_BR, 0xd63f0000, 0 },
	{ "blraa", K_PAC, 0xd73f0800, 4 },
	{ "blraaz", K_PAC, 0xd63f081f, 2 },
	{ "blrab", K_PAC, 0xd73f0c00, 4 },
	{ "blrabz", K_PAC, 0xd63f0c1f, 2 },
	{ "br", K_BR, 0xd61f0000, 0 },
	{ "braa", K_PAC, 0xd71f0800, 4 },
	{ "braaz", K_PAC, 0xd61f081f, 2 },
	{ "brab", K_PAC, 0xd71f0c00, 4 },
	{ "brabz", K_PAC, 0xd61f0c1f, 2 },
	{ "brk", K_EXC, 0xd4200000, 0 },
	{ "bsl", K_V3, 0x2e601c00, 0 },
	{ "bti", K_BTI, 0xd503241f, 0 },
	{ "cbnz", K_CBZ, 0x35000000, 0 },
	{ "cbz", K_CBZ, 0x34000000, 0 },
	{ "ccmn", K_CCMP, 0x3a400000, 0 },
	{ "ccmp", K_CCMP, 0x7a400000, 0 },
	{ "chkfeat", K_PAC, 0xd503251f, 8 },
	{ "cinc", K_CNEG, 0x1a800400, 0 },
	{ "cinv", K_CNEG, 0x5a800000, 0 },
	{ "clrbhb", K_FIXED, 0xd50322df, 0 },
	{ "clrex", K_FIXED, 0xd5033f5f, 0 },
	{ "cls", K_DP1, 0x5ac01400, 0 },
	{ "clz", K_DP1, 0x5ac01000, 0 },
	{ "cmeq", K_VPERM, 0x2e208c00, 0 },
	{ "cmge", K_VPERM, 0x0e203c00, 0 },
	{ "cmgt", K_VPERM, 0x0e203400, 0 },
	{ "cmhi", K_VPERM, 0x2e203400, 0 },
	{ "cmhs", K_VPERM, 0x2e203c00, 0 },
	{ "cmn", K_CMP, 1, 0 },
	{ "cmp", K_CMP, 3, 0 },
	{ "cmpp", K_PAC, 0xbac0001f, 20 },
	{ "cmtst", K_VPERM, 0x0e208c00, 0 },
	{ "cneg", K_CNEG, 0x5a800400, 0 },
	{ "cnt", K_IALIAS, 0x5ac01c00, 3 },
	{ "cpye", K_CPY, 0x1d800400, 0 },
	{ "cpyen", K_CPY, 0x1d800400, 12 },
	{ "cpyern", K_CPY, 0x1d800400, 8 },
	{ "cpyert", K_CPY, 0x1d800400, 2 },
	{ "cpyewn", K_CPY, 0x1d800400, 4 },
	{ "cpyewt", K_CPY, 0x1d800400, 1 },
	{ "cpyfe", K_CPY, 0x19800400, 0 },
	{ "cpyfen", K_CPY, 0x19800400, 12 },
	{ "cpyfern", K_CPY, 0x19800400, 8 },
	{ "cpyfert", K_CPY, 0x19800400, 2 },
	{ "cpyfewn", K_CPY, 0x19800400, 4 },
	{ "cpyfewt", K_CPY, 0x19800400, 1 },
	{ "cpyfm", K_CPY, 0x19400400, 0 },
	{ "cpyfmn", K_CPY, 0x19400400, 12 },
	{ "cpyfmrn", K_CPY, 0x19400400, 8 },
	{ "cpyfmrt", K_CPY, 0x19400400, 2 },
	{ "cpyfmwn", K_CPY, 0x19400400, 4 },
	{ "cpyfmwt", K_CPY, 0x19400400, 1 },
	{ "cpyfp", K_CPY, 0x19000400, 0 },
	{ "cpyfpn", K_CPY, 0x19000400, 12 },
	{ "cpyfprn", K_CPY, 0x19000400, 8 },
	{ "cpyfprt", K_CPY, 0x19000400, 2 },
	{ "cpyfpwn", K_CPY, 0x19000400, 4 },
	{ "cpyfpwt", K_CPY, 0x19000400, 1 },
	{ "cpym", K_CPY, 0x1d400400, 0 },
	{ "cpymn", K_CPY, 0x1d400400, 12 },
	{ "cpymrn", K_CPY, 0x1d400400, 8 },
	{ "cpymrt", K_CPY, 0x1d400400, 2 },
	{ "cpymwn", K_CPY, 0x1d400400, 4 },
	{ "cpymwt", K_CPY, 0x1d400400, 1 },
	{ "cpyp", K_CPY, 0x1d000400, 0 },
	{ "cpypn", K_CPY, 0x1d000400, 12 },
	{ "cpyprn", K_CPY, 0x1d000400, 8 },
	{ "cpyprt", K_CPY, 0x1d000400, 2 },
	{ "cpypwn", K_CPY, 0x1d000400, 4 },
	{ "cpypwt", K_CPY, 0x1d000400, 1 },
	{ "crc32b", K_CRC, 0x1ac04000, 0 },
	{ "crc32cb", K_CRC, 0x1ac05000, 0 },
	{ "crc32ch", K_CRC, 0x1ac05400, 0 },
	{ "crc32cw", K_CRC, 0x1ac05800, 0 },
	{ "crc32cx", K_CRC, 0x9ac05c00, 1 },
	{ "crc32h", K_CRC, 0x1ac04400, 0 },
	{ "crc32w", K_CRC, 0x1ac04800, 0 },
	{ "crc32x", K_CRC, 0x9ac04c00, 1 },
	{ "csdb", K_FIXED, 0xd503229f, 0 },
	{ "csel", K_CSEL, 0x1a800000, 0 },
	{ "cset", K_CSET, 0x1a9f07e0, 0 },
	{ "csetm", K_CSET, 0x5a9f03e0, 0 },
	{ "csinc", K_CSEL, 0x1a800400, 0 },
	{ "csinv", K_CSEL, 0x5a800000, 0 },
	{ "csneg", K_CSEL, 0x5a800400, 0 },
	{ "ctz", K_IALIAS, 0x5ac01800, 3 },
	{ "dc", K_DC, 0, 0 },
	{ "dgh", K_FIXED, 0xd50320df, 0 },
	{ "dmb", K_BARRIER, 0xd50330bf, 0 },
	{ "dsb", K_BARRIER, 0xd503309f, 0 },
	{ "dup", K_DUP, 0, 0 },
	{ "eon", K_LOGIC, 2, 1 },
	{ "eor", K_LOGIC, 2, 0 },
	{ "eor3", K_SHA512, 0xce000000, 3 },
	{ "eret", K_FIXED, 0xd69f03e0, 0 },
	{ "eretaa", K_FIXED, 0xd69f0bff, 0 },
	{ "eretab", K_FIXED, 0xd69f0fff, 0 },
	{ "esb", K_FIXED, 0xd503221f, 0 },
	{ "ext", K_EXT, 0x2e000000, 0 },
	{ "extr", K_EXTR, 0x13800000, 0 },
	{ "fabs", K_FP, 0x1e20c000, 2 },
	{ "fadd", K_FP, 0x1e202800, 1 },
	{ "fccmp", K_FP, 0x1e200400, 5 },
	{ "fccmpe", K_FP, 0x1e200410, 5 },
	{ "fcmp", K_FP, 0x1e202000, 4 },
	{ "fcmpe", K_FP, 0x1e202010, 4 },
	{ "fcsel", K_FP, 0x1e200c00, 6 },
	{ "fcvt", K_FP, 0x1e224000, 9 },
	{ "fcvtas", K_FP, 0x1e240000, 7 },
	{ "fcvtau", K_FP, 0x1e250000, 7 },
	{ "fcvtms", K_FP, 0x1e300000, 7 },
	{ "fcvtmu", K_FP, 0x1e310000, 7 },
	{ "fcvtns", K_FP, 0x1e200000, 7 },
	{ "fcvtnu", K_FP, 0x1e210000, 7 },
	{ "fcvtps", K_FP, 0x1e280000, 7 },
	{ "fcvtpu", K_FP, 0x1e290000, 7 },
	{ "fcvtzs", K_FP, 0x1e380000, 7 },
	{ "fcvtzu", K_FP, 0x1e390000, 7 },
	{ "fdiv", K_FP, 0x1e201800, 1 },
	{ "fmadd", K_FP, 0x1f000000, 3 },
	{ "fmax", K_FP, 0x1e204800, 1 },
	{ "fmaxnm", K_FP, 0x1e206800, 1 },
	{ "fmin", K_FP, 0x1e205800, 1 },
	{ "fminnm", K_FP, 0x1e207800, 1 },
	{ "fmov", K_FMOV, 0, 0 },
	{ "fmsub", K_FP, 0x1f008000, 3 },
	{ "fmul", K_FP, 0x1e200800, 1 },
	{ "fneg", K_FP, 0x1e214000, 2 },
	{ "fnmadd", K_FP, 0x1f200000, 3 },
	{ "fnmsub", K_FP, 0x1f208000, 3 },
	{ "fnmul", K_FP, 0x1e208800, 1 },
	{ "frinta", K_FP, 0x1e264000, 2 },
	{ "frinti", K_FP, 0x1e27c000, 2 },
	{ "frintm", K_FP, 0x1e254000, 2 },
	{ "frintn", K_FP, 0x1e244000, 2 },
	{ "frintp", K_FP, 0x1e24c000, 2 },
	{ "frintx", K_FP, 0x1e274000, 2 },
	{ "frintz", K_FP, 0x1e25c000, 2 },
	{ "fsqrt", K_FP, 0x1e21c000, 2 },
	{ "fsub", K_FP, 0x1e203800, 1 },
	{ "gmi", K_PAC, 0x9ac01400, 5 },
	{ "hint", K_HINT, 0xd503201f, 0 },
	{ "hlt", K_EXC, 0xd4400000, 0 },
	{ "hvc", K_EXC, 0xd4000002, 0 },
	{ "ic", K_IC, 0, 0 },
	{ "ins", K_INS, 0, 0 },
	{ "irg", K_PAC, 0x9ac01000, 16 },
	{ "isb", K_BARRIER, 0xd50330df, 1 },
	{ "ld1", K_VLD1, 0x0c400000, 0 },
	{ "ld1r", K_LDR_REP, 0x0d40c000, 1 },
	{ "ld2", K_VLD1, 0x0c400000, 2 },
	{ "ld2r", K_LDR_REP, 0x0d60c000, 2 },
	{ "ld3", K_VLD1, 0x0c400000, 3 },
	{ "ld3r", K_LDR_REP, 0x0d40e000, 3 },
	{ "ld4", K_VLD1, 0x0c400000, 4 },
	{ "ld4r", K_LDR_REP, 0x0d60e000, 4 },
	{ "ldapur", K_PAC, 0x99400000, 12 },
	{ "ldapurb", K_PAC, 0x19400000, 11 },
	{ "ldapurh", K_PAC, 0x59400000, 11 },
	{ "ldapursb", K_PAC, 0x19800000, 13 },
	{ "ldapursh", K_PAC, 0x59800000, 13 },
	{ "ldapursw", K_PAC, 0x99800000, 14 },
	{ "ldaxr", K_LDX, 0x085ffc00, 0 },
	{ "ldg", K_MTE, 0xd9600000, 1 },
	{ "ldgm", K_MTEM, 0xd9e00000, 0 },
	{ "ldnp", K_LDP, 0, 1 | 2 },
	{ "ldp", K_LDP, 0, 1 },
	{ "ldpsw", K_LDP, 0, 1 | 4 },
	{ "ldr", K_LDST, 0x01, 0xff },
	{ "ldraa", K_PAC, 0xf8200400, 6 },
	{ "ldrab", K_PAC, 0xf8a00400, 6 },
	{ "ldrb", K_LDST, 0x01, 0 },
	{ "ldrh", K_LDST, 0x01, 1 },
	{ "ldrsb", K_LDST, 0x02, 0 },
	{ "ldrsh", K_LDST, 0x02, 1 },
	{ "ldrsw", K_LDST, 0x02, 2 },
	{ "ldtr", K_LDSTT, 0x01, 0xff },
	{ "ldtrb", K_LDSTT, 0x01, 0 },
	{ "ldtrh", K_LDSTT, 0x01, 1 },
	{ "ldur", K_LDSTU, 0x01, 0xff },
	{ "ldurb", K_LDSTU, 0x01, 0 },
	{ "ldurh", K_LDSTU, 0x01, 1 },
	{ "ldursb", K_LDSTU, 0x02, 0 },
	{ "ldursh", K_LDSTU, 0x02, 1 },
	{ "ldursw", K_LDSTU, 0x02, 2 },
	{ "ldxr", K_LDX, 0x085f7c00, 0 },
	{ "lsl", K_SHIFT, 0, 0 },
	{ "lsr", K_SHIFT, 1, 0 },
	{ "madd", K_MUL, 0x1b000000, 1 },
	{ "mneg", K_MUL, 0x1b00fc00, 0 },
	{ "mov", K_MOV, 0, 0 },
	{ "movi", K_MOVI, 0, 0 },
	{ "movk", K_MOVW, 0x72800000, 0 },
	{ "movn", K_MOVW, 0x12800000, 0 },
	{ "movz", K_MOVW, 0x52800000, 0 },
	{ "mrs", K_MRS, 0xd5300000, 0 },
	{ "msr", K_MSR, 0xd5100000, 0 },
	{ "msub", K_MUL, 0x1b008000, 1 },
	{ "mul", K_MUL, 0x1b007c00, 0 },
	{ "mvn", K_NEG, 0x2a2003e0, 1 },
	{ "mvni", K_MOVI, 0x00000000, 1 },
	{ "neg", K_NEG, 0x4b0003e0, 0 },
	{ "negs", K_NEG, 0x6b0003e0, 0 },
	{ "ngc", K_IALIAS, 0x5a0003e0, 2 },
	{ "ngcs", K_IALIAS, 0x7a0003e0, 2 },
	{ "nop", K_FIXED, 0xd503201f, 0 },
	{ "not", K_VNOT, 0x2e205800, 0 },
	{ "orn", K_LOGIC, 1, 1 },
	{ "orr", K_LOGIC, 1, 0 },
	{ "pacda", K_PAC, 0xdac10800, 3 },
	{ "pacdb", K_PAC, 0xdac10c00, 3 },
	{ "pacdza", K_PAC, 0xdac12be0, 1 },
	{ "pacdzb", K_PAC, 0xdac12fe0, 1 },
	{ "pacga", K_PAC, 0x9ac03000, 5 },
	{ "pacia", K_PAC, 0xdac10000, 3 },
	{ "pacia1716", K_FIXED, 0xd503211f, 0 },
	{ "paciasp", K_FIXED, 0xd503233f, 0 },
	{ "paciaz", K_FIXED, 0xd503231f, 0 },
	{ "pacib", K_PAC, 0xdac10400, 3 },
	{ "pacib1716", K_FIXED, 0xd503215f, 0 },
	{ "pacibsp", K_FIXED, 0xd503237f, 0 },
	{ "pacibz", K_FIXED, 0xd503235f, 0 },
	{ "paciza", K_PAC, 0xdac123e0, 1 },
	{ "pacizb", K_PAC, 0xdac127e0, 1 },
	{ "pfalse", K_PREG, 0x2518e400, 0 },
	{ "pmul", K_V3, 0x2e209c00, 1 },
	{ "pmull", K_PMULL, 0x0e20e000, 0 },
	{ "pmull2", K_PMULL, 0x4e20e000, 0 },
	{ "prfm", K_PRFM, 0, 0 },
	{ "prfum", K_PAC, 0xf8800000, 15 },
	{ "psb", K_PAC, 0xd503223f, 7 },
	{ "pssbb", K_FIXED, 0xd503349f, 0 },
	{ "rbit", K_DP1, 0x5ac00000, 0 },
	{ "rdffr", K_PREG, 0x2519f000, 0 },
	{ "rdsvl", K_RDVL, 0x04bf5800, 0 },
	{ "rdvl", K_RDVL, 0x04bf5000, 0 },
	{ "ret", K_RET, 0xd65f0000, 0 },
	{ "retaa", K_FIXED, 0xd65f0bff, 0 },
	{ "retab", K_FIXED, 0xd65f0fff, 0 },
	{ "rev", K_DP1, 0x5ac00800, 1 },
	{ "rev16", K_VREV, 0x0e201800, 0x5ac00400 },
	{ "rev32", K_VREV, 0x2e200800, 0xdac00800 },
	{ "rev64", K_VREV, 0x0e200800, 0xdac00c00 },
	{ "ror", K_SHIFT, 3, 0 },
	{ "saddlv", K_ACROSS, 0x0e303800, 1 },
	{ "sb", K_FIXED, 0xd50330ff, 0 },
	{ "sbc", K_DP2, 0x5a000000, 0 },
	{ "sbcs", K_DP2, 0x7a000000, 0 },
	{ "sbfiz", K_BFI, 0, 0 },
	{ "sbfm", K_BFM, 0, 0 },
	{ "sbfx", K_BFX, 0, 0 },
	{ "scvtf", K_FP, 0x1e220000, 8 },
	{ "sdiv", K_DP2, 0x1ac00c00, 0 },
	{ "sete", K_SET, 0x19c08400, 0 },
	{ "seten", K_SET, 0x19c08400, 2 },
	{ "setet", K_SET, 0x19c08400, 1 },
	{ "setetn", K_SET, 0x19c08400, 3 },
	{ "setffr", K_FIXED, 0x252c9000, 0 },
	{ "setm", K_SET, 0x19c04400, 0 },
	{ "setmn", K_SET, 0x19c04400, 2 },
	{ "setmt", K_SET, 0x19c04400, 1 },
	{ "setmtn", K_SET, 0x19c04400, 3 },
	{ "setp", K_SET, 0x19c00400, 0 },
	{ "setpn", K_SET, 0x19c00400, 2 },
	{ "setpt", K_SET, 0x19c00400, 1 },
	{ "setptn", K_SET, 0x19c00400, 3 },
	{ "sev", K_FIXED, 0xd503209f, 0 },
	{ "sevl", K_FIXED, 0xd50320bf, 0 },
	{ "sha1c", K_SHA3, 0x5e000000, 0 },
	{ "sha1h", K_SHA2, 0x5e280800, 0 },
	{ "sha1m", K_SHA3, 0x5e002000, 0 },
	{ "sha1p", K_SHA3, 0x5e001000, 0 },
	{ "sha1su0", K_SHA3, 0x5e003000, 1 },
	{ "sha1su1", K_SHA2, 0x5e281800, 1 },
	{ "sha256h", K_SHA3, 0x5e004000, 2 },
	{ "sha256h2", K_SHA3, 0x5e005000, 2 },
	{ "sha256su0", K_SHA2, 0x5e282800, 1 },
	{ "sha256su1", K_SHA3, 0x5e006000, 1 },
	{ "sha512h", K_SHA512, 0xce608000, 0 },
	{ "sha512h2", K_SHA512, 0xce608400, 0 },
	{ "sha512su0", K_SHA512, 0xcec08000, 1 },
	{ "sha512su1", K_SHA512, 0xce608800, 2 },
	{ "shl", K_VSHIFT, 0x0f005400, 0 },
	{ "sli", K_VSHIFT, 0x2f005400, 0 },
	{ "smaddl", K_MUL, 0x9b200000, 3 },
	{ "smax", K_IALIAS, 0x1ac06000, 4 },
	{ "smaxv", K_ACROSS, 0x0e30a800, 0 },
	{ "smc", K_EXC, 0xd4000003, 0 },
	{ "smin", K_IALIAS, 0x1ac06800, 4 },
	{ "sminv", K_ACROSS, 0x0e31a800, 0 },
	{ "smnegl", K_MUL, 0x9b20fc00, 2 },
	{ "smov", K_SMOV, 0x0e002c00, 0 },
	{ "smsubl", K_MUL, 0x9b208000, 3 },
	{ "smulh", K_DP2, 0x9b407c00, 1 },
	{ "smull", K_MUL, 0x9b207c00, 2 },
	{ "sri", K_VSHIFT, 0x2f004400, 1 },
	{ "srshr", K_VSHIFT, 0x0f002400, 1 },
	{ "srsra", K_VSHIFT, 0x0f003400, 1 },
	{ "ssbb", K_FIXED, 0xd503309f, 0 },
	{ "sshr", K_VSHIFT, 0x0f000400, 1 },
	{ "ssra", K_VSHIFT, 0x0f001400, 1 },
	{ "st1", K_VLD1, 0x0c000000, 0 },
	{ "st2", K_VLD1, 0x0c000000, 2 },
	{ "st2g", K_MTE, 0xd9a00000, 0 },
	{ "st3", K_VLD1, 0x0c000000, 3 },
	{ "st4", K_VLD1, 0x0c000000, 4 },
	{ "stg", K_MTE, 0xd9200000, 0 },
	{ "stgm", K_MTEM, 0xd9a00000, 0 },
	{ "stgp", K_PAC, 0x69000000, 21 },
	{ "stlur", K_PAC, 0x99000000, 12 },
	{ "stlurb", K_PAC, 0x19000000, 11 },
	{ "stlurh", K_PAC, 0x59000000, 11 },
	{ "stlxr", K_STX, 0x0800fc00, 0 },
	{ "stnp", K_LDP, 0, 2 },
	{ "stp", K_LDP, 0, 0 },
	{ "str", K_LDST, 0x00, 0xff },
	{ "strb", K_LDST, 0x00, 0 },
	{ "strh", K_LDST, 0x00, 1 },
	{ "sttr", K_LDSTT, 0x00, 0xff },
	{ "sttrb", K_LDSTT, 0x00, 0 },
	{ "sttrh", K_LDSTT, 0x00, 1 },
	{ "stur", K_LDSTU, 0x00, 0xff },
	{ "sturb", K_LDSTU, 0x00, 0 },
	{ "sturh", K_LDSTU, 0x00, 1 },
	{ "stxr", K_STX, 0x08007c00, 0 },
	{ "stz2g", K_MTE, 0xd9e00000, 0 },
	{ "stzg", K_MTE, 0xd9600000, 0 },
	{ "stzgm", K_MTEM, 0xd9200000, 0 },
	{ "sub", K_ADDSUB, 2, 0 },
	{ "subg", K_PAC, 0xd1800000, 17 },
	{ "subp", K_PAC, 0x9ac00000, 5 },
	{ "subps", K_PAC, 0xbac00000, 5 },
	{ "subs", K_ADDSUB, 3, 0 },
	{ "svc", K_EXC, 0xd4000001, 0 },
	{ "sxtb", K_IALIAS, 0, 0x07 },
	{ "sxth", K_IALIAS, 0, 0x0f },
	{ "sxtw", K_IALIAS, 0, 0x1f },
	{ "sys", K_SYS, 0xd5080000, 0 },
	{ "sysl", K_PAC, 0xd5280000, 10 },
	{ "tbl", K_TBL, 0x0e000000, 0 },
	{ "tbnz", K_TBZ, 0x37000000, 0 },
	{ "tbx", K_TBL, 0x0e001000, 0 },
	{ "tbz", K_TBZ, 0x36000000, 0 },
	{ "tlbi", K_TLBI, 0, 0 },
	{ "trn1", K_VPERM, 0x0e002800, 0 },
	{ "trn2", K_VPERM, 0x0e006800, 0 },
	{ "tsb", K_PAC, 0xd503225f, 7 },
	{ "tst", K_TST, 0, 0 },
	{ "uadalp", K_VMISC, 0x2e206800, 1 },
	{ "uaddlp", K_VMISC, 0x2e202800, 1 },
	{ "uaddlv", K_ACROSS, 0x2e303800, 1 },
	{ "ubfiz", K_BFI, 2, 0 },
	{ "ubfm", K_BFM, 2, 0 },
	{ "ubfx", K_BFX, 2, 0 },
	{ "ucvtf", K_FP, 0x1e230000, 8 },
	{ "udiv", K_DP2, 0x1ac00800, 0 },
	{ "umaddl", K_MUL, 0x9ba00000, 3 },
	{ "umax", K_IALIAS, 0x1ac06400, 4 },
	{ "umaxv", K_ACROSS, 0x2e30a800, 0 },
	{ "umin", K_IALIAS, 0x1ac06c00, 4 },
	{ "uminv", K_ACROSS, 0x2e31a800, 0 },
	{ "umnegl", K_MUL, 0x9ba0fc00, 2 },
	{ "umov", K_UMOV, 0, 0 },
	{ "umsubl", K_MUL, 0x9ba08000, 3 },
	{ "umulh", K_DP2, 0x9bc07c00, 1 },
	{ "umull", K_MUL, 0x9ba07c00, 2 },
	{ "uqsub", K_VPERM, 0x2e202c00, 0 },
	{ "urshr", K_VSHIFT, 0x2f002400, 1 },
	{ "ursra", K_VSHIFT, 0x2f003400, 1 },
	{ "ushr", K_VSHIFT, 0x2f000400, 1 },
	{ "usra", K_VSHIFT, 0x2f001400, 1 },
	{ "uxtb", K_IALIAS, 2, 0x07 },
	{ "uxth", K_IALIAS, 2, 0x0f },
	{ "uxtw", K_IALIAS, 2, 0x1f },
	{ "uzp1", K_VPERM, 0x0e001800, 0 },
	{ "uzp2", K_VPERM, 0x0e005800, 0 },
	{ "wfe", K_FIXED, 0xd503205f, 0 },
	{ "wfet", K_PAC, 0xd5031000, 1 },
	{ "wfi", K_FIXED, 0xd503207f, 0 },
	{ "wfit", K_PAC, 0xd5031020, 1 },
	{ "wrffr", K_PREG, 0x25289000, 5 },
	{ "xpacd", K_PAC, 0xdac147e0, 1 },
	{ "xpaci", K_PAC, 0xdac143e0, 1 },
	{ "xpaclri", K_FIXED, 0xd50320ff, 0 },
	{ "yield", K_FIXED, 0xd503203f, 0 },
	{ "zip1", K_VPERM, 0x0e003800, 0 },
	{ "zip2", K_VPERM, 0x0e007800, 0 },
};

/* case-insensitive hash table over mns (built once) */
#define MNH 1024
static uint16_t mnh[MNH];	/* index + 1, 0 = empty */
static pthread_once_t mnh_once = PTHREAD_ONCE_INIT;

#define mn_hash lc_hash

static void mnh_build(void)
{
	for (size_t i = 0; i < sizeof mns / sizeof mns[0]; i++) {
		uint32_t j = mn_hash(mns[i].name, strlen(mns[i].name)) & (MNH - 1);
		while (mnh[j]) j = (j + 1) & (MNH - 1);
		mnh[j] = (uint16_t)(i + 1);
	}
}

static const Mn *lookup(const char *s, size_t n)
{
	pthread_once(&mnh_once, mnh_build);
	for (uint32_t j = mn_hash(s, n) & (MNH - 1); mnh[j]; j = (j + 1) & (MNH - 1)) {
		const Mn *m = &mns[mnh[j] - 1];
		if (!lc_cmp(s, n, m->name)) return m;
	}
	return 0;
}

/* ---------------------------------------------------------------- integer data processing */

static int addsub_imm(Ctx *c, int sf, int op, int s, unsigned rd, unsigned rn, const A64Op *im, const A64Op *sh)
{
	int lsl12 = 0;
	if (sh) {
		if (sh->kind != A64O_SHIFT || sh->shift != A64_LSL || (sh->imm != 0 && sh->imm != 12)) BADOPS;
		lsl12 = sh->imm == 12;
	}
	if (im->hassym || im->mod) {
		uint32_t r;
		switch (im->mod) {
		case A64_MOD_LO12: r = R_AARCH64_ADD_ABS_LO12_NC; break;
		case A64_MOD_TLSDESC_LO12: r = R_AARCH64_TLSDESC_ADD_LO12; break;
		case A64_MOD_TPREL_HI12: r = R_AARCH64_TLSLE_ADD_TPREL_HI12; lsl12 = 1; break;
		case A64_MOD_TPREL_LO12: r = R_AARCH64_TLSLE_ADD_TPREL_LO12; break;
		case A64_MOD_TPREL_LO12_NC: r = R_AARCH64_TLSLE_ADD_TPREL_LO12_NC; break;
		case A64_MOD_DTPREL_HI12: r = R_AARCH64_TLSLD_ADD_DTPREL_HI12; lsl12 = 1; break;
		case A64_MOD_DTPREL_LO12: r = R_AARCH64_TLSLD_ADD_DTPREL_LO12; break;
		case A64_MOD_DTPREL_LO12_NC: r = R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC; break;
		default: FAIL("invalid immediate");
		}
		*c->out = a64_addsub_imm(sf, op, s, rd, rn, 0, lsl12);
		return setfix(c, r, im);
	}
	int64_t v = im->imm;
	if (!sh && v < 0 && v > -4096 * 4096LL) { v = -v; op ^= 1; }
	if (!sh && v > 0xfff && !(v & 0xfff) && v <= 0xfff000) { v >>= 12; lsl12 = 1; }
	if (v < 0 || v > 0xfff) FAIL("expected compatible register, symbol or integer in range [0, 4095]");
	*c->out = a64_addsub_imm(sf, op, s, rd, rn, (unsigned)v, lsl12);
	return 0;
}

static int ext_option(const A64Op *sh, int sf, unsigned *opt, unsigned *amt, int rm_w)
{
	unsigned o, a = 0;
	if (!sh) { o = rm_w ? 2 : (sf ? 3 : 2); }
	else if (sh->kind != A64O_SHIFT) return -1;
	else {
		switch (sh->shift) {
		case A64_LSL: o = sf ? 3 : 2; break;
		case A64_UXTB: o = 0; break; case A64_UXTH: o = 1; break; case A64_UXTW: o = 2; break; case A64_UXTX: o = 3; break;
		case A64_SXTB: o = 4; break; case A64_SXTH: o = 5; break; case A64_SXTW: o = 6; break; case A64_SXTX: o = 7; break;
		default: return -1;
		}
		a = sh->hasamt ? (unsigned)sh->imm : 0;
		if (a > 4) return -1;
	}
	*opt = o; *amt = a;
	return 0;
}

static int addsub_reg(Ctx *c, int sf, int op, int s, const A64Op *d, const A64Op *n, const A64Op *m, const A64Op *sh)
{
	int ext = issp(d) || issp(n) || (sf && isw(m)) || (sh && sh->shift >= A64_UXTB);
	if (ext) {
		unsigned opt, amt;
		if (ext_option(sh, sf, &opt, &amt, isw(m))) BADOPS;
		*c->out = 0x0b200000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | R(m) << 16 | opt << 13 | amt << 10 | R(n) << 5 | R(d);
		return 0;
	}
	unsigned st = 0, amt = 0;
	if (sh) {
		if (sh->kind != A64O_SHIFT || sh->shift > A64_ASR) BADOPS;
		st = sh->shift; amt = (unsigned)sh->imm;
		if (amt >= (sf ? 64u : 32u)) BADOPS;
	}
	*c->out = a64_addsub_reg(sf, op, s, R(d), R(n), R(m), st, amt);
	return 0;
}

static int vec3(Ctx *c, uint32_t base, int sized)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || o[2].kind != A64O_VREG) BADOPS;
	int sz = arr_size(o[0].arr);
	if (sz < 0 || sz > 3) BADOPS;
	*c->out = base | arr_q(o[0].arr) << 30 | (sized ? (uint32_t)sz << 22 : 0) | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_addsub(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	int op = (int)(m->base >> 1), s = (int)(m->base & 1);
	if (c->n == 3 && o[0].kind == A64O_VREG) return vec3(c, op ? 0x2e208400 : 0x0e208400, 1);
	if (c->n == 3 && isreg(&o[0]) && o[0].cls == A64_D) {
		*c->out = (op ? 0x7ee08400u : 0x5ee08400u) | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (c->n < 3 || c->n > 4 || !isgpr(&o[0]) || !isgpr(&o[1])) BADOPS;
	int sf = isx(&o[0]);
	if (isx(&o[1]) != sf) BADOPS;
	if (isimm(&o[2])) {
		if ((s && issp(&o[0])) || (!s && iszr(&o[0])) || iszr(&o[1])) BADOPS;
		return addsub_imm(c, sf, op, s, R(&o[0]), R(&o[1]), &o[2], c->n == 4 ? &o[3] : 0);
	}
	if (!isgpr(&o[2])) BADOPS;
	return addsub_reg(c, sf, op, s, &o[0], &o[1], &o[2], c->n == 4 ? &o[3] : 0);
}

static int k_cmp(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	int op = m->base == 3;	/* cmp = subs, cmn = adds */
	if (c->n < 2 || c->n > 3 || !isgpr(&o[0])) BADOPS;
	int sf = isx(&o[0]);
	A64Op zr = o[0]; zr.reg = 31; zr.sp = 0;
	if (isimm(&o[1])) return addsub_imm(c, sf, op, 1, 31, R(&o[0]), &o[1], c->n == 3 ? &o[2] : 0);
	if (!isgpr(&o[1])) BADOPS;
	return addsub_reg(c, sf, op, 1, &zr, &o[0], &o[1], c->n == 3 ? &o[2] : 0);
}

static int k_vnot(Ctx *c, const Mn *m);

static int k_neg(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (m->x && c->n == 2 && o[0].kind == A64O_VREG) {	/* mvn Vd.T, Vn.T = not */
		static const Mn vnot = { "not", K_VNOT, 0x2e205800, 0 };
		return k_vnot(c, &vnot);
	}
	if (c->n < 2 || c->n > 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1])) BADOPS;
	int sf = isx(&o[0]);
	unsigned st = 0, amt = 0;
	if (c->n == 3) {
		if (o[2].kind != A64O_SHIFT || o[2].shift > (m->x ? A64_ROR : A64_ASR)) BADOPS;
		st = o[2].shift; amt = (unsigned)o[2].imm;
	}
	*c->out = m->base | (uint32_t)sf << 31 | st << 22 | R(&o[1]) << 16 | amt << 10 | R(&o[0]);
	return 0;
}

static int logic_reg(Ctx *c, int sf, unsigned opc, int n, unsigned rd, unsigned rn, const A64Op *rm, const A64Op *sh)
{
	unsigned st = 0, amt = 0;
	if (sh) {
		if (sh->kind != A64O_SHIFT || sh->shift > A64_ROR) BADOPS;
		st = sh->shift; amt = (unsigned)sh->imm;
		if (amt >= (sf ? 64u : 32u)) BADOPS;
	}
	*c->out = a64_logic_reg(sf, opc, n, rd, rn, R(rm), st, amt);
	return 0;
}

static int k_logic(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	unsigned opc = m->base;
	int inv = (int)m->x;
	if (o[0].kind == A64O_VREG && c->n >= 2 && iscimm(&o[1]) && (opc == 1 && !inv ? 1 : opc == 0 && inv)) {
		/* orr/bic Vd.T, #imm8{, lsl #n}: modified immediate */
		int sz = arr_size(o[0].arr);
		unsigned sh = 0, cmode;
		uint64_t v = (uint64_t)o[1].imm;
		if (c->n == 3) {
			if (o[2].kind != A64O_SHIFT || o[2].shift != A64_LSL) BADOPS;
			sh = (unsigned)o[2].imm;
		} else if (c->n != 2) BADOPS;
		if (v > 0xff) FAIL("immediate out of range");
		if (sz == 1) { if (sh != 0 && sh != 8) BADOPS; cmode = 9 | (sh / 8) << 1; }
		else if (sz == 2) { if (sh & 7 || sh > 24) BADOPS; cmode = 1 | (sh / 8) << 1; }
		else BADOPS;
		*c->out = 0x0f000400u | arr_q(o[0].arr) << 30 | (unsigned)inv << 29 | (unsigned)(v >> 5 & 7) << 16 | cmode << 12 | (unsigned)(v & 31) << 5 | R(&o[0]);
		return 0;
	}
	if (c->n == 3 && o[0].kind == A64O_VREG) {
		static const uint32_t vb[4][2] = { { 0x0e201c00, 0x0e601c00 }, { 0x0ea01c00, 0x0ee01c00 }, { 0x2e201c00, 0 }, { 0, 0 } };
		uint32_t b = vb[opc][inv];
		if (!b) BADOPS;
		if (o[1].kind != A64O_VREG || o[2].kind != A64O_VREG) BADOPS;
		*c->out = b | arr_q(o[0].arr) << 30 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (c->n < 3 || c->n > 4 || !isgpr(&o[0]) || !gpr_nosp(&o[1])) BADOPS;
	int sf = isx(&o[0]);
	if (isimm(&o[2])) {
		if (!iscimm(&o[2]) || c->n != 3) BADOPS;
		if (opc == 3 ? issp(&o[0]) : iszr(&o[0])) BADOPS;
		uint64_t v = (uint64_t)o[2].imm;
		if (inv) v = ~v;
		if (!sf) v &= 0xffffffffu;
		int e = a64_logimm(v, sf);
		if (e < 0) FAIL("expected compatible register or logical immediate");
		*c->out = a64_logic_imm(sf, opc, R(&o[0]), R(&o[1]), (unsigned)e);
		return 0;
	}
	if (!gpr_nosp(&o[0]) || !gpr_nosp(&o[2])) BADOPS;
	return logic_reg(c, sf, opc, inv, R(&o[0]), R(&o[1]), &o[2], c->n == 4 ? &o[3] : 0);
}

static int k_tst(Ctx *c, const Mn *m)
{
	(void)m;
	const A64Op *o = c->op;
	if (c->n < 2 || c->n > 3 || !gpr_nosp(&o[0])) BADOPS;
	int sf = isx(&o[0]);
	if (isimm(&o[1])) {
		if (!iscimm(&o[1])) BADOPS;
		uint64_t v = (uint64_t)o[1].imm;
		if (!sf) v &= 0xffffffffu;
		int e = a64_logimm(v, sf);
		if (e < 0) FAIL("expected compatible register or logical immediate");
		*c->out = a64_logic_imm(sf, 3, 31, R(&o[0]), (unsigned)e);
		return 0;
	}
	return logic_reg(c, sf, 3, 0, 31, R(&o[0]), &o[1], c->n == 3 ? &o[2] : 0);
}

static int movz_fits(uint64_t v, int sf, unsigned *imm, unsigned *hw)
{
	for (unsigned s = 0; s < (sf ? 4u : 2u); s++)
		if (!(v & ~(0xffffull << 16 * s))) { *imm = (unsigned)(v >> 16 * s) & 0xffff; *hw = s; return 1; }
	return 0;
}

static int mov_imm(Ctx *c, int sf, unsigned rd, int64_t val, int rd_sp)
{
	uint64_t v = (uint64_t)val, mask = sf ? ~0ull : 0xffffffffull;
	if (!sf) {
		if ((v >> 32) && (v >> 32) != 0xffffffffu) FAIL("expected compatible register or logical immediate");
		v &= mask;
	}
	unsigned imm, hw;
	if (!rd_sp) {
		if (movz_fits(v, sf, &imm, &hw)) { *c->out = a64_movz(sf, rd, imm, hw); return 0; }
		if (movz_fits(~v & mask, sf, &imm, &hw)) {
			/* MOVN: for W, LLVM refuses when the value is 0xffffxxxx form that movz covers */
			*c->out = a64_movn(sf, rd, imm, hw);
			return 0;
		}
	}
	int e = a64_logimm(v, sf);
	if (e < 0) FAIL("expected compatible register or logical immediate");
	*c->out = a64_logic_imm(sf, 1, rd, 31, (unsigned)e);
	return 0;
}

static int velem_imm5(const A64Op *o, unsigned *imm5)
{
	int sz = arr_size(o->arr);
	if (sz < 0 || sz > 3) return -1;
	*imm5 = ((unsigned)o->idx << (sz + 1) | 1u << sz) & 31;
	return sz;
}

static int k_umov(Ctx *c, const Mn *m)
{
	(void)m;
	const A64Op *o = c->op;
	unsigned imm5;
	if (c->n != 2 || !gpr_nosp(&o[0]) || o[1].kind != A64O_VELEM) BADOPS;
	int sz = velem_imm5(&o[1], &imm5);
	if (sz < 0) BADOPS;
	*c->out = 0x0e003c00u | (uint32_t)(sz == 3) << 30 | imm5 << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_mov(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2) BADOPS;
	if (o[0].kind == A64O_VREG && o[1].kind == A64O_VREG) {
		*c->out = 0x0ea01c00u | arr_q(o[0].arr) << 30 | R(&o[1]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (o[0].kind == A64O_VELEM) {
		unsigned imm5;
		int sz = velem_imm5(&o[0], &imm5);
		if (sz < 0) BADOPS;
		if (isgpr(&o[1])) { *c->out = 0x4e001c00u | imm5 << 16 | R(&o[1]) << 5 | R(&o[0]); return 0; }
		if (o[1].kind == A64O_VELEM) {
			unsigned imm4 = ((unsigned)o[1].idx << sz) & 15;
			*c->out = 0x6e000400u | imm5 << 16 | imm4 << 11 | R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		BADOPS;
	}
	if (o[1].kind == A64O_VELEM) {
		if (isgpr(&o[0])) return k_umov(c, m);
		if (isreg(&o[0])) {	/* dup scalar */
			unsigned imm5;
			if (velem_imm5(&o[1], &imm5) < 0) BADOPS;
			*c->out = 0x5e000400u | imm5 << 16 | R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		BADOPS;
	}
	if (!isgpr(&o[0])) BADOPS;
	int sf = isx(&o[0]);
	if (isimm(&o[1])) {
		if (!iscimm(&o[1])) {
			if (o[1].mod) {	/* mov with :abs_gN: behaves like movz */
				Ctx d = *c; (void)d;
			}
			FAIL("expected compatible register or logical immediate");
		}
		return mov_imm(c, sf, R(&o[0]), o[1].imm, issp(&o[0]));
	}
	if (!isgpr(&o[1]) || isx(&o[1]) != sf) BADOPS;
	if (issp(&o[0]) || issp(&o[1])) { *c->out = a64_addsub_imm(sf, 0, 0, R(&o[0]), R(&o[1]), 0, 0); return 0; }
	*c->out = a64_logic_reg(sf, 1, 0, R(&o[0]), 31, R(&o[1]), 0, 0);
	return 0;
}

static int k_movw(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n < 2 || c->n > 3 || !gpr_nosp(&o[0]) || !isimm(&o[1])) BADOPS;
	int sf = isx(&o[0]);
	unsigned hw = 0;
	if (c->n == 3) {
		if (o[2].kind != A64O_SHIFT || o[2].shift != A64_LSL || (o[2].imm & 15) || o[2].imm > (sf ? 48 : 16)) BADOPS;
		hw = (unsigned)o[2].imm / 16;
	}
	uint32_t base = m->base | (uint32_t)sf << 31;
	if (o[1].mod) {
		static const struct { uint8_t hw; uint8_t sgn; uint32_t r; } g[] = {
			[A64_MOD_ABS_G0] = { 0, 0, R_AARCH64_MOVW_UABS_G0 }, [A64_MOD_ABS_G0_NC] = { 0, 0, R_AARCH64_MOVW_UABS_G0_NC },
			[A64_MOD_ABS_G1] = { 1, 0, R_AARCH64_MOVW_UABS_G1 }, [A64_MOD_ABS_G1_NC] = { 1, 0, R_AARCH64_MOVW_UABS_G1_NC },
			[A64_MOD_ABS_G2] = { 2, 0, R_AARCH64_MOVW_UABS_G2 }, [A64_MOD_ABS_G2_NC] = { 2, 0, R_AARCH64_MOVW_UABS_G2_NC },
			[A64_MOD_ABS_G3] = { 3, 0, R_AARCH64_MOVW_UABS_G3 },
			[A64_MOD_ABS_G0_S] = { 0, 1, R_AARCH64_MOVW_SABS_G0 }, [A64_MOD_ABS_G1_S] = { 1, 1, R_AARCH64_MOVW_SABS_G1 },
			[A64_MOD_ABS_G2_S] = { 2, 1, R_AARCH64_MOVW_SABS_G2 },
			[A64_MOD_GOTTPREL_G1] = { 1, 0, R_AARCH64_TLSIE_MOVW_GOTTPREL_G1 },
			[A64_MOD_GOTTPREL_G0_NC] = { 0, 0, R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC },
			[A64_MOD_TPREL_G2] = { 2, 0, R_AARCH64_TLSLE_MOVW_TPREL_G2 }, [A64_MOD_TPREL_G1] = { 1, 0, R_AARCH64_TLSLE_MOVW_TPREL_G1 },
			[A64_MOD_TPREL_G1_NC] = { 1, 0, R_AARCH64_TLSLE_MOVW_TPREL_G1_NC }, [A64_MOD_TPREL_G0] = { 0, 0, R_AARCH64_TLSLE_MOVW_TPREL_G0 },
			[A64_MOD_TPREL_G0_NC] = { 0, 0, R_AARCH64_TLSLE_MOVW_TPREL_G0_NC },
			[A64_MOD_DTPREL_G2] = { 2, 0, R_AARCH64_TLSLD_MOVW_DTPREL_G2 }, [A64_MOD_DTPREL_G1] = { 1, 0, R_AARCH64_TLSLD_MOVW_DTPREL_G1 },
			[A64_MOD_DTPREL_G1_NC] = { 1, 0, R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC }, [A64_MOD_DTPREL_G0] = { 0, 0, R_AARCH64_TLSLD_MOVW_DTPREL_G0 },
			[A64_MOD_DTPREL_G0_NC] = { 0, 0, R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC },
			[A64_MOD_PREL_G3] = { 3, 0, R_AARCH64_MOVW_PREL_G3 }, [A64_MOD_PREL_G2] = { 2, 0, R_AARCH64_MOVW_PREL_G2 },
			[A64_MOD_PREL_G2_NC] = { 2, 0, R_AARCH64_MOVW_PREL_G2_NC }, [A64_MOD_PREL_G1] = { 1, 0, R_AARCH64_MOVW_PREL_G1 },
			[A64_MOD_PREL_G1_NC] = { 1, 0, R_AARCH64_MOVW_PREL_G1_NC }, [A64_MOD_PREL_G0] = { 0, 0, R_AARCH64_MOVW_PREL_G0 },
			[A64_MOD_PREL_G0_NC] = { 0, 0, R_AARCH64_MOVW_PREL_G0_NC },
		};
		if (o[1].mod >= sizeof g / sizeof g[0] || !g[o[1].mod].r || c->n == 3) FAIL("invalid relocation specifier");
		hw = g[o[1].mod].hw;
		base |= hw << 21;
		if (!o[1].hassym) {	/* resolved now, as the backend would */
			int64_t v = o[1].imm;
			if (g[o[1].mod].sgn && v < 0) { v = ~v; base &= ~(1u << 30); }
			*c->out = base | (uint32_t)((uint64_t)v >> 16 * hw & 0xffff) << 5 | R(&o[0]);
			return 0;
		}
		*c->out = base | R(&o[0]);
		switch (o[1].mod) {	/* MC: a checked TLS group in movz is written as movn */
		case A64_MOD_GOTTPREL_G1: case A64_MOD_TPREL_G2: case A64_MOD_TPREL_G1: case A64_MOD_TPREL_G0:
		case A64_MOD_DTPREL_G2: case A64_MOD_DTPREL_G1: case A64_MOD_DTPREL_G0:
			if ((*c->out & 0x60000000u) == 0x40000000u) *c->out &= ~0x40000000u;
			break;
		}
		return setfix(c, g[o[1].mod].r, &o[1]);
	}
	if (o[1].hassym) FAIL("expected relocated symbol or integer");
	int64_t v = o[1].imm;
	if (v < 0 || v > 0xffff) {
		/* movz Rd, #imm with an implied shift */
		unsigned imm, h;
		if (c->n == 2 && movz_fits((uint64_t)v & (sf ? ~0ull : 0xffffffffull), sf, &imm, &h) && m->base != 0x72800000) {
			*c->out = base | h << 21 | imm << 5 | R(&o[0]);
			return 0;
		}
		FAIL("immediate out of range");
	}
	*c->out = base | hw << 21 | (uint32_t)v << 5 | R(&o[0]);
	return 0;
}

static int k_shift(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1])) BADOPS;
	int sf = isx(&o[0]);
	unsigned size = sf ? 64 : 32;
	if (isgpr(&o[2])) {
		*c->out = a64_dp2(sf, 8 + m->base, R(&o[0]), R(&o[1]), R(&o[2]));
		return 0;
	}
	if (!iscimm(&o[2])) BADOPS;
	unsigned s = (unsigned)o[2].imm;
	if (s >= size) FAIL("immediate out of range");
	switch (m->base) {
	case 0: *c->out = a64_bfm(sf, 2, R(&o[0]), R(&o[1]), (size - s) & (size - 1), size - 1 - s); return 0;
	case 1: *c->out = a64_bfm(sf, 2, R(&o[0]), R(&o[1]), s, size - 1); return 0;
	case 2: *c->out = a64_bfm(sf, 0, R(&o[0]), R(&o[1]), s, size - 1); return 0;
	default:
		*c->out = 0x13800000u | (uint32_t)sf << 31 | (uint32_t)sf << 22 | R(&o[1]) << 16 | s << 10 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
}

static int bf_ops(Ctx *c, int *sf, unsigned *a, unsigned *b)
{
	const A64Op *o = c->op;
	if (c->n != 4 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || !iscimm(&o[2]) || !iscimm(&o[3])) return -1;
	*sf = isx(&o[0]);
	*a = (unsigned)o[2].imm; *b = (unsigned)o[3].imm;
	return 0;
}

static int k_bfm(Ctx *c, const Mn *m)
{
	int sf; unsigned r, s;
	if (bf_ops(c, &sf, &r, &s)) BADOPS;
	*c->out = a64_bfm(sf, m->base, R(&c->op[0]), R(&c->op[1]), r, s);
	return 0;
}

static int k_bfx(Ctx *c, const Mn *m)
{
	int sf; unsigned lsb, w;
	if (bf_ops(c, &sf, &lsb, &w)) BADOPS;
	unsigned size = sf ? 64 : 32;
	if (!w || lsb + w > size) FAIL("requested extract overflows register");
	*c->out = a64_bfm(sf, m->base, R(&c->op[0]), R(&c->op[1]), lsb, lsb + w - 1);
	return 0;
}

static int k_bfi(Ctx *c, const Mn *m)
{
	int sf; unsigned lsb, w;
	if (bf_ops(c, &sf, &lsb, &w)) BADOPS;
	unsigned size = sf ? 64 : 32;
	if (!w || lsb + w > size) FAIL("requested insert overflows register");
	*c->out = a64_bfm(sf, m->base, R(&c->op[0]), R(&c->op[1]), (size - lsb) & (size - 1), w - 1);
	return 0;
}

static int k_extr(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 4 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || !gpr_nosp(&o[2]) || !iscimm(&o[3])) BADOPS;
	int sf = isx(&o[0]);
	*c->out = m->base | (uint32_t)sf << 31 | (uint32_t)sf << 22 | R(&o[2]) << 16 | (unsigned)o[3].imm << 10 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_vnot(Ctx *c, const Mn *m);

static int k_dp1(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n == 2 && o[0].kind == A64O_VREG && m->base == 0x5ac00000) {	/* rbit Vd.8b|16b, Vn */
		static const Mn vrbit = { "rbit", K_VNOT, 0x2e605800, 0 };
		return k_vnot(c, &vrbit);
	}
	if (c->n != 2 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1])) BADOPS;
	int sf = isx(&o[0]);
	uint32_t b = m->base | (uint32_t)sf << 31;
	if (m->x && sf) b |= 0x400;	/* rev: X form is opc 11 */
	*c->out = b | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_dp2(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || !gpr_nosp(&o[2])) BADOPS;
	if (m->x && (!isx(&o[0]) || !isx(&o[1]) || !isx(&o[2]))) BADOPS;
	*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_mul(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	int n = m->x == 1 || m->x == 3 ? 4 : 3;
	if (c->n != n) BADOPS;
	for (int i = 0; i < n; i++) if (!gpr_nosp(&o[i])) BADOPS;
	if (m->x >= 2) {
		if (!isx(&o[0]) || !isw(&o[1]) || !isw(&o[2]) || (n == 4 && !isx(&o[3]))) BADOPS;
		*c->out = m->base | R(&o[2]) << 16 | (n == 4 ? R(&o[3]) << 10 : 0) | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | R(&o[2]) << 16 | (n == 4 ? R(&o[3]) << 10 : 0) | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_crc(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || !isw(&o[0]) || !isw(&o[1]) || !gpr_nosp(&o[2]) || isx(&o[2]) != (int)m->x) BADOPS;
	*c->out = m->base | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int getcond(const A64Op *o)
{
	if (o->kind != A64O_NAME) return -1;
	return a64_cond(o->name, o->nlen);
}

static int k_csel(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 4 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || !gpr_nosp(&o[2])) BADOPS;
	int cc = getcond(&o[3]);
	if (cc < 0) FAIL("expected AArch64 condition code");
	*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | R(&o[2]) << 16 | (unsigned)cc << 12 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_cset(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || !gpr_nosp(&o[0])) BADOPS;
	int cc = getcond(&o[1]);
	if (cc < 0 || cc >= 14) FAIL("expected AArch64 condition code");
	*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | (unsigned)(cc ^ 1) << 12 | R(&o[0]);
	return 0;
}

static int k_cneg(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1])) BADOPS;
	int cc = getcond(&o[2]);
	if (cc < 0 || cc >= 14) FAIL("expected AArch64 condition code");
	*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | R(&o[1]) << 16 | (unsigned)(cc ^ 1) << 12 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_ccmp(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 4 || !gpr_nosp(&o[0]) || !iscimm(&o[2])) BADOPS;
	int cc = getcond(&o[3]);
	if (cc < 0) FAIL("expected AArch64 condition code");
	uint32_t w = m->base | (uint32_t)isx(&o[0]) << 31 | (unsigned)cc << 12 | R(&o[0]) << 5 | ((unsigned)o[2].imm & 15);
	if (iscimm(&o[1])) {
		int64_t v = o[1].imm;
		if (v < 0 && v >= -31) { v = -v; w ^= 0x40000000; }	/* ccmp #-n -> ccmn #n */
		if (v < 0 || v > 31) FAIL("immediate out of range");
		w |= 0x800 | (unsigned)v << 16;
	} else if (gpr_nosp(&o[1])) w |= R(&o[1]) << 16;
	else BADOPS;
	*c->out = w;
	return 0;
}

/* ---------------------------------------------------------------- branches */

static int k_adr(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || !isx(&o[0]) || o[0].sp || !isimm(&o[1])) BADOPS;
	*c->out = m->base | R(&o[0]);
	if (!o[1].hassym) {	/* a constant is the pc-relative offset (adrp: in pages) */
		if (o[1].mod) FAIL("invalid relocation specifier");
		int64_t v = o[1].imm;
		if (m->x) {
			if (v & 0xfff) FAIL("fixup not sufficiently aligned");
			v >>= 12;
		}
		if (v < -(1 << 20) || v >= (1 << 20)) FAIL("fixup value out of range");
		*c->out |= (uint32_t)(v & 3) << 29 | (uint32_t)((v >> 2) & 0x7ffff) << 5;
		return 0;
	}
	if (m->x) {
		if (o[1].mod == A64_MOD_GOT) return setfix(c, R_AARCH64_ADR_GOT_PAGE, &o[1]);
		if (o[1].mod == A64_MOD_TLVP) return setfix(c, R_VCC_TLVP_PAGE21, &o[1]);
		if (o[1].mod == A64_MOD_TLSDESC) return setfix(c, R_AARCH64_TLSDESC_ADR_PAGE21, &o[1]);
		if (o[1].mod == A64_MOD_GOTTPREL) return setfix(c, R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21, &o[1]);
		if (o[1].mod == A64_MOD_PG_HI21_NC) return setfix(c, R_AARCH64_ADR_PREL_PG_HI21_NC, &o[1]);
		if (o[1].mod && o[1].mod != A64_MOD_PG_HI21) FAIL("invalid relocation specifier");
		return setfix(c, R_AARCH64_ADR_PREL_PG_HI21, &o[1]);
	}
	if (o[1].mod) FAIL("invalid relocation specifier");
	return setfix(c, R_AARCH64_ADR_PREL_LO21, &o[1]);
}

static int k_b(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 1 || !isimm(&o[0]) || o[0].mod) BADOPS;
	*c->out = m->base;
	return setfix(c, m->x, &o[0]);
}

static int bcond(Ctx *c, int cc)
{
	const A64Op *o = c->op;
	if (c->n != 1 || !isimm(&o[0]) || o[0].mod) BADOPS;
	*c->out = 0x54000000u | (unsigned)cc;
	return setfix(c, R_AARCH64_CONDBR19, &o[0]);
}

static int k_cbz(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || !gpr_nosp(&o[0]) || !isimm(&o[1])) BADOPS;
	*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | R(&o[0]);
	return setfix(c, R_AARCH64_CONDBR19, &o[1]);
}

static int k_tbz(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || !gpr_nosp(&o[0]) || !iscimm(&o[1]) || !isimm(&o[2])) BADOPS;
	unsigned bit = (unsigned)o[1].imm;
	if (bit >= (isx(&o[0]) ? 64u : 32u)) FAIL("immediate out of range");
	*c->out = m->base | (bit >> 5) << 31 | (bit & 31) << 19 | R(&o[0]);
	return setfix(c, R_AARCH64_TSTBR14, &o[2]);
}

static int k_br(Ctx *c, const Mn *m)
{
	if (c->n != 1 || !isx(&c->op[0]) || c->op[0].sp) BADOPS;
	*c->out = m->base | R(&c->op[0]) << 5;
	return 0;
}

static int k_ret(Ctx *c, const Mn *m)
{
	unsigned r = 30;
	if (c->n == 1) { if (!isx(&c->op[0]) || c->op[0].sp) BADOPS; r = R(&c->op[0]); }
	else if (c->n) BADOPS;
	*c->out = m->base | r << 5;
	return 0;
}

/* ---------------------------------------------------------------- loads and stores */

/* size (log2 bytes), V, and opc for a register operand of ldr/str */
static int ldst_reg(const A64Op *r, int load, unsigned *size, unsigned *v, unsigned *opc)
{
	if (!isreg(r) || (isgpr(r) && r->sp)) return -1;
	*v = 0; *opc = (unsigned)load;
	switch (r->cls) {
	case A64_X: *size = 3; break;
	case A64_W: *size = 2; break;
	case A64_B: *size = 0; *v = 1; break;
	case A64_H: *size = 1; *v = 1; break;
	case A64_S: *size = 2; *v = 1; break;
	case A64_D: *size = 3; *v = 1; break;
	case A64_Q: *size = 0; *v = 1; *opc = 2 | (unsigned)load; return 4;
	default: return -1;
	}
	return (int)*size;
}

static const uint32_t lo12_reloc[] = {
	R_AARCH64_LDST8_ABS_LO12_NC, R_AARCH64_LDST16_ABS_LO12_NC, R_AARCH64_LDST32_ABS_LO12_NC,
	R_AARCH64_LDST64_ABS_LO12_NC, R_AARCH64_LDST128_ABS_LO12_NC,
};

static int ldst_common(Ctx *c, unsigned size, unsigned v, unsigned opc, int scale, unsigned rt, const A64Op *mem, const A64Op *post, int unscaled_only)
{
	unsigned rn = mem->reg;
	if (post) {
		if (mem->mode != A64M_OFF || mem->hasamt || !iscimm(post)) return -1;
		int64_t off = post->imm;
		if (off < -256 || off > 255) { *c->err = "index must be an integer in range [-256, 255]."; return -2; }
		*c->out = a64_ldst_imm9(size, (int)v, opc, 1, rt, rn, (int)off);
		return 0;
	}
	if (mem->mode == A64M_REG) {
		if (unscaled_only) return -1;
		unsigned option, s = 0;
		switch (mem->shift) {
		case A64_LSL: option = 3; if (mem->idxcls != A64_X) return -1; break;
		case A64_UXTW: option = 2; if (mem->idxcls != A64_W) return -1; break;
		case A64_SXTW: option = 6; if (mem->idxcls != A64_W) return -1; break;
		case A64_SXTX: option = 7; if (mem->idxcls != A64_X) return -1; break;
		default: return -1;
		}
		if (mem->hasamt == 3) {
			if (mem->imm == scale) s = 1;
			else if (mem->imm != 0) return -1;
			else s = 0;
			if (mem->shift == A64_LSL && mem->imm == 0) s = 0;
		}
		*c->out = 0x38200800u | size << 30 | v << 26 | opc << 22 | (unsigned)mem->index << 16 | option << 13 | s << 12 | rn << 5 | rt;
		return 0;
	}
	if ((mem->mod == A64_MOD_GOT_LO12 || mem->mod == A64_MOD_TLVP_LO12) && scale == 3 && opc == 1 && !v &&
	    mem->mode == A64M_OFF && !unscaled_only) {
		*c->out = a64_ldst_uoff(size, 0, opc, rt, rn, 0);
		c->fix->type = mem->mod == A64_MOD_TLVP_LO12 ? R_VCC_TLVP_PAGEOFF12 : R_AARCH64_LD64_GOT_LO12_NC;
		c->fix->v = mem->v;
		return 0;
	}
	if ((mem->mod == A64_MOD_GOTTPREL_LO12_NC || mem->mod == A64_MOD_TLSDESC_LO12) && scale == 3 && opc == 1 && !v &&
	    mem->mode == A64M_OFF && !unscaled_only) {
		*c->out = a64_ldst_uoff(size, 0, opc, rt, rn, 0);
		c->fix->type = mem->mod == A64_MOD_TLSDESC_LO12 ? R_AARCH64_TLSDESC_LD64_LO12 : R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC;
		c->fix->v = mem->v;
		return 0;
	}
	if (mem->mod >= A64_MOD_TPREL_LO12 && mem->mod <= A64_MOD_DTPREL_LO12_NC && mem->mod != A64_MOD_DTPREL_G2 &&
	    (mem->mod == A64_MOD_TPREL_LO12 || mem->mod == A64_MOD_TPREL_LO12_NC || mem->mod == A64_MOD_DTPREL_LO12 || mem->mod == A64_MOD_DTPREL_LO12_NC)) {
		if (mem->mode != A64M_OFF || unscaled_only) return -1;
		int nc = mem->mod == A64_MOD_TPREL_LO12_NC || mem->mod == A64_MOD_DTPREL_LO12_NC;
		int tp = mem->mod == A64_MOD_TPREL_LO12 || mem->mod == A64_MOD_TPREL_LO12_NC;
		uint32_t r = scale == 4 ? (tp ? 570u : 572u) : (tp ? R_AARCH64_TLSLE_LDST8_TPREL_LO12 : R_AARCH64_TLSLD_LDST8_DTPREL_LO12) + 2u * (unsigned)scale;
		*c->out = a64_ldst_uoff(size, (int)v, opc, rt, rn, 0);
		c->fix->type = r + (unsigned)nc; c->fix->v = mem->v;
		return 0;
	}
	if (mem->hassym || mem->mod) {
		if (mem->mod != A64_MOD_LO12 || mem->mode != A64M_OFF || unscaled_only) return -1;
		*c->out = a64_ldst_uoff(size, (int)v, opc, rt, rn, 0);
		c->fix->type = lo12_reloc[scale]; c->fix->v = mem->v;
		return 0;
	}
	int64_t off = mem->imm;
	if (mem->mode == A64M_PRE) {
		if (off < -256 || off > 255) { *c->err = "index must be an integer in range [-256, 255]."; return -2; }
		*c->out = a64_ldst_imm9(size, (int)v, opc, 3, rt, rn, (int)off);
		return 0;
	}
	int64_t unit = (int64_t)1 << scale;
	if (!unscaled_only && off >= 0 && !(off & (unit - 1)) && off / unit < 4096) {
		*c->out = a64_ldst_uoff(size, (int)v, opc, rt, rn, (unsigned)(off / unit));
		return 0;
	}
	if (off >= -256 && off <= 255) {
		*c->out = a64_ldst_imm9(size, (int)v, opc, 0, rt, rn, (int)off);
		return 0;
	}
	*c->err = "index must be a multiple of the access size in range";
	return -2;
}

static int k_ldst(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	int load = (int)(m->base & 1), sgn = m->base == 2;
	unsigned size, v, opc;
	int scale;
	if (c->n < 2 || c->n > 3) BADOPS;
	if (m->x == 0xff && c->n == 2 && isreg(&o[0]) && (o[0].cls == A64_Z || o[0].cls == A64_P)) {
		/* SVE ldr/str Zt|Pt, [Xn{, #imm, mul vl}] */
		const A64Op *mm = &o[1];
		if (o[0].arr || mm->kind != A64O_MEM || mm->mode != A64M_OFF || mm->hassym || mm->mod ||
		    (mm->imm && !mm->mulvl) || mm->imm < -256 || mm->imm > 255) BADOPS;
		uint32_t i9 = (uint32_t)mm->imm & 0x1ff;
		*c->out = (load ? 0x85800000u : 0xe5800000u) | (o[0].cls == A64_Z ? 0x4000u : 0) |
			  (i9 >> 3) << 16 | (i9 & 7) << 10 | (unsigned)mm->reg << 5 | R(&o[0]);
		return 0;
	}
	if (m->x == 0xff) {
		/* ldr literal */
		if (load && c->n == 2 && isimm(&o[1])) {
			if (!isreg(&o[0]) || (isgpr(&o[0]) && o[0].sp)) BADOPS;
			uint32_t b;
			switch (o[0].cls) {
			case A64_W: b = 0x18000000; break; case A64_X: b = 0x58000000; break;
			case A64_S: b = 0x1c000000; break; case A64_D: b = 0x5c000000; break; case A64_Q: b = 0x9c000000; break;
			default: BADOPS;
			}
			*c->out = b | R(&o[0]);
			if (!o[1].hassym) {	/* a constant is the pc-relative offset */
				int64_t v = o[1].imm;
				if (o[1].mod || (v & 3) || v < -(1 << 20) || v >= (1 << 20)) FAIL("fixup value out of range");
				*c->out |= (uint32_t)((v >> 2) & 0x7ffff) << 5;
				return 0;
			}
			if (o[1].mod == A64_MOD_GOT) return setfix(c, R_AARCH64_GOT_LD_PREL19, &o[1]);
			if (o[1].mod == A64_MOD_GOTTPREL) return setfix(c, R_AARCH64_TLSIE_LD_GOTTPREL_PREL19, &o[1]);
			if (o[1].mod == A64_MOD_TLSDESC) return setfix(c, R_AARCH64_LD_PREL_LO19, &o[1]);	/* as MC */
			if (o[1].mod) FAIL("unsupported literal");
			return setfix(c, R_AARCH64_LD_PREL_LO19, &o[1]);
		}
		scale = ldst_reg(&o[0], load, &size, &v, &opc);
		if (scale < 0) BADOPS;
	} else {
		if (!gpr_nosp(&o[0])) BADOPS;
		size = m->x; v = 0; scale = (int)size;
		if (sgn) { opc = isx(&o[0]) ? 2 : 3; if (m->x == 2 && !isx(&o[0])) BADOPS; }
		else { if (!isw(&o[0])) BADOPS; opc = (unsigned)load; }
	}
	if (o[1].kind != A64O_MEM) BADOPS;
	int r = ldst_common(c, size, v, opc, scale, R(&o[0]), &o[1], c->n == 3 ? &o[2] : 0, 0);
	if (r == -1) BADOPS;
	return r < 0 ? -1 : 0;
}

static int k_ldstu(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	int load = (int)(m->base & 1);
	unsigned size, v, opc;
	int scale;
	if (c->n != 2 || o[1].kind != A64O_MEM || o[1].mode != A64M_OFF) BADOPS;
	if (m->x == 0xff) { scale = ldst_reg(&o[0], load, &size, &v, &opc); if (scale < 0) BADOPS; }
	else if (m->base == 2) {	/* ldurs[bhw] */
		if (!gpr_nosp(&o[0]) || (m->x == 2 && !isx(&o[0]))) BADOPS;
		size = m->x; v = 0; opc = isx(&o[0]) ? 2 : 3; scale = (int)size;
	}
	else { if (!isw(&o[0])) BADOPS; size = m->x; v = 0; opc = (unsigned)load; scale = (int)size; }
	int r = ldst_common(c, size, v, opc, scale, R(&o[0]), &o[1], 0, 1);
	if (r == -1) BADOPS;
	return r < 0 ? -1 : 0;
}

static int k_ldstt(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	unsigned size;
	if (c->n != 2 || o[1].kind != A64O_MEM || o[1].mode != A64M_OFF || o[1].hassym || !gpr_nosp(&o[0])) BADOPS;
	if (m->x == 0xff) size = isx(&o[0]) ? 3 : 2;
	else { if (!isw(&o[0])) BADOPS; size = m->x; }
	int64_t off = o[1].imm;
	if (off < -256 || off > 255) FAIL("index must be an integer in range [-256, 255].");
	*c->out = 0x38000800u | size << 30 | (m->base & 1) << 22 | ((uint32_t)off & 0x1ff) << 12 | (unsigned)o[1].reg << 5 | R(&o[0]);
	return 0;
}

static int k_ldp(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	int load = m->x & 1, nt = (m->x & 2) != 0;
	if (c->n < 3 || c->n > 4 || !isreg(&o[0]) || !isreg(&o[1]) || o[0].cls != o[1].cls || o[2].kind != A64O_MEM) BADOPS;
	unsigned opc, v = 0, scale;
	switch (o[0].cls) {
	case A64_W: opc = 0; scale = 2; break;
	case A64_X: opc = 2; scale = 3; break;
	case A64_S: opc = 0; v = 1; scale = 2; break;
	case A64_D: opc = 1; v = 1; scale = 3; break;
	case A64_Q: opc = 2; v = 1; scale = 4; break;
	default: BADOPS;
	}
	if (m->x & 4) { if (o[0].cls != A64_X) BADOPS; opc = 1; scale = 2; }	/* ldpsw */
	if (isgpr(&o[0]) && (o[0].sp || o[1].sp)) BADOPS;
	const A64Op *mem = &o[2];
	unsigned mode;
	int64_t off;
	if (c->n == 4) {
		if (mem->mode != A64M_OFF || mem->hasamt || !iscimm(&o[3]) || nt) BADOPS;
		mode = 1; off = o[3].imm;
	} else {
		if (mem->mode == A64M_REG || mem->hassym) BADOPS;
		mode = mem->mode == A64M_PRE ? 3 : 2;
		if (nt && mode != 2) BADOPS;
		off = mem->imm;
	}
	if (nt) mode = 0;
	int64_t unit = (int64_t)1 << scale;
	if (off & (unit - 1) || off / unit < -64 || off / unit > 63) FAIL("index must be a multiple of the register size in range");
	*c->out = a64_ldstp(opc, (int)v, mode, load, R(&o[0]), R(&o[1]), mem->reg, (int)(off / unit));
	return 0;
}

static int k_ldx(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || !gpr_nosp(&o[0]) || o[1].kind != A64O_MEM || o[1].mode != A64M_OFF || o[1].imm || o[1].hassym) BADOPS;
	*c->out = m->base | (isx(&o[0]) ? 0xc0000000u : 0x80000000u) | (unsigned)o[1].reg << 5 | R(&o[0]);
	return 0;
}

static int k_stx(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || !isw(&o[0]) || o[0].sp || !gpr_nosp(&o[1]) || o[2].kind != A64O_MEM || o[2].mode != A64M_OFF || o[2].imm) BADOPS;
	*c->out = m->base | (isx(&o[1]) ? 0xc0000000u : 0x80000000u) | R(&o[0]) << 16 | (unsigned)o[2].reg << 5 | R(&o[1]);
	return 0;
}

static int prfop(const char *s, size_t n)
{
	static const char *t[] = { "pld", "pli", "pst" };
	static const char *tg[] = { "l1", "l2", "l3" };
	if (n != 9) return -1;
	int ty = -1, lv = -1, pol;
	for (int i = 0; i < 3; i++) if (!strncasecmp(s, t[i], 3)) ty = i;
	for (int i = 0; i < 3; i++) if (!strncasecmp(s + 3, tg[i], 2)) lv = i;
	if (!strncasecmp(s + 5, "keep", 4)) pol = 0;
	else if (!strncasecmp(s + 5, "strm", 4)) pol = 1;
	else return -1;
	if (ty < 0 || lv < 0) return -1;
	return ty << 3 | lv << 1 | pol;
}

static int k_prfm(Ctx *c, const Mn *m)
{
	(void)m;
	const A64Op *o = c->op;
	if (c->n != 2 || o[1].kind != A64O_MEM) BADOPS;
	int op;
	if (o[0].kind == A64O_NAME) op = prfop(o[0].name, o[0].nlen);
	else if (iscimm(&o[0])) op = (int)o[0].imm;
	else BADOPS;
	if (op < 0 || op > 31) FAIL("prefetch hint expected");
	/* prfm = ldr x-sized with opc 2 */
	int r = ldst_common(c, 3, 0, 2, 3, (unsigned)op, &o[1], 0, 0);
	if (r == -1) BADOPS;
	return r < 0 ? -1 : 0;
}

/* ld1/st1 (multiple structures) */
static int k_vld1(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n < 2 || c->n > 3 || o[0].kind != A64O_LIST || o[1].kind != A64O_MEM || o[1].mode != A64M_OFF || o[1].hasamt) BADOPS;
	static const uint32_t opcode[5] = { 0, 0x7000, 0xa000, 0x6000, 0x2000 };
	unsigned nr = o[0].nregs, ns = m->x ? m->x : 1;	/* ld1: 1-4 registers; ldN: N */
	if (nr < 1 || nr > 4 || (m->x && nr != m->x)) BADOPS;
	int sz = arr_size(o[0].arr);
	uint32_t w;
	if (o[0].hasidx) {	/* one lane: {Vt.T, ...}[i] (element arrangement) */
		unsigned i = o[0].idx, qs, op3, sb;
		switch (o[0].arr) {
		case A64_ELB: if (i > 15) BADOPS; op3 = 0; qs = i >> 2; sb = i & 3; break;
		case A64_ELH: if (i > 7) BADOPS; op3 = 2; qs = i >> 1; sb = (i & 1) << 1; break;
		case A64_ELS: if (i > 3) BADOPS; op3 = 4; qs = i; sb = 0; break;
		case A64_ELD: if (i > 1) BADOPS; op3 = 4; qs = i << 1; sb = 1; break;
		default: BADOPS;
		}
		if (ns == 3 || ns == 4) op3 |= 1;
		w = 0x0d000000u | (m->base & 0x00400000u) | (qs >> 1) << 30 | (uint32_t)(ns == 2 || ns == 4) << 21 | op3 << 13 |
		    (qs & 1) << 12 | sb << 10 | (unsigned)o[1].reg << 5 | R(&o[0]);
		if (c->n == 3) {
			w |= 0x00800000;
			if (iscimm(&o[2])) {
				if (o[2].imm != (int64_t)ns << sz) FAIL("invalid post-increment");
				w |= 31u << 16;
			} else if (gpr_nosp(&o[2]) && isx(&o[2]) && o[2].reg != 31) w |= R(&o[2]) << 16;
			else BADOPS;
		}
		*c->out = w;
		return 0;
	}
	if (sz < 0 || sz > 3 || (m->x && sz == 3 && !arr_q(o[0].arr))) BADOPS;
	static const uint32_t sop[5] = { 0, 0, 0x8000, 0x4000, 0x0000 };
	w = m->base | arr_q(o[0].arr) << 30 | (m->x ? sop[nr] : opcode[nr]) | (unsigned)sz << 10 | (unsigned)o[1].reg << 5 | R(&o[0]);
	if (c->n == 3) {
		w |= 0x00800000;
		if (iscimm(&o[2])) {
			if (o[2].imm != (int64_t)nr * arr_bits(o[0].arr) / 8) FAIL("invalid post-increment");
			w |= 31u << 16;
		} else if (gpr_nosp(&o[2]) && isx(&o[2]) && o[2].reg != 31) w |= R(&o[2]) << 16;
		else BADOPS;
	}
	*c->out = w;
	return 0;
}

/* ---------------------------------------------------------------- system */

static int k_fixed(Ctx *c, const Mn *m)
{
	if (c->n) BADOPS;
	*c->out = m->base;
	return 0;
}

static int k_hint(Ctx *c, const Mn *m)
{
	if (c->n != 1 || !iscimm(&c->op[0]) || c->op[0].imm < 0 || c->op[0].imm > 127) BADOPS;
	*c->out = m->base | (unsigned)c->op[0].imm << 5;
	return 0;
}

static int k_bti(Ctx *c, const Mn *m)
{
	unsigned t = 0;
	if (c->n == 1) {
		const A64Op *o = c->op;
		if (o->kind != A64O_NAME) BADOPS;
		if (o->nlen == 1 && (o->name[0] | 32) == 'c') t = 1;
		else if (o->nlen == 1 && (o->name[0] | 32) == 'j') t = 2;
		else if (o->nlen == 2 && !strncasecmp(o->name, "jc", 2)) t = 3;
		else FAIL("invalid BTI target");
	} else if (c->n) BADOPS;
	*c->out = m->base | t << 6;
	return 0;
}

static int k_exc(Ctx *c, const Mn *m)
{
	unsigned v = 0;
	if (c->n == 1) {
		if (!iscimm(&c->op[0]) || c->op[0].imm < 0 || c->op[0].imm > 0xffff) BADOPS;
		v = (unsigned)c->op[0].imm;
	} else if (c->n) BADOPS;
	*c->out = m->base | v << 5;
	return 0;
}

static int barrier_opt(const A64Op *o)
{
	static const char *t[16] = { 0, "oshld", "oshst", "osh", 0, "nshld", "nshst", "nsh", 0, "ishld", "ishst", "ish", 0, "ld", "st", "sy" };
	if (o->kind == A64O_NAME) {
		for (int i = 0; i < 16; i++) if (t[i] && strlen(t[i]) == o->nlen && !strncasecmp(o->name, t[i], o->nlen)) return i;
		return -1;
	}
	if (iscimm(o) && o->imm >= 0 && o->imm < 16) return (int)o->imm;
	return -1;
}

static int k_barrier(Ctx *c, const Mn *m)
{
	int op = 15;
	if (c->n == 1) {
		op = barrier_opt(&c->op[0]);
		if (op < 0) FAIL("invalid barrier option");
		if (m->x && op != 15 && c->op[0].kind == A64O_NAME) FAIL("'sy' or #imm operand expected");
	} else if (c->n || !m->x) BADOPS;
	*c->out = (m->base & ~0xf00u) | (unsigned)op << 8;
	return 0;
}

static const struct { const char *n; uint8_t op1, crn, crm, op2, reg; } pstate[] = {
	{ "spsel", 0, 4, 0, 5, 0 }, { "daifset", 3, 4, 0, 6, 0 }, { "daifclr", 3, 4, 0, 7, 0 },
	{ "uao", 0, 4, 0, 3, 0 }, { "pan", 0, 4, 0, 4, 0 }, { "dit", 3, 4, 0, 2, 0 }, { "ssbs", 3, 4, 0, 1, 0 },
	{ "tco", 3, 4, 0, 4, 0 },
};

static int k_msr(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || o[0].kind != A64O_NAME) BADOPS;
	if (iscimm(&o[1])) {
		for (size_t i = 0; i < sizeof pstate / sizeof pstate[0]; i++) {
			if (strlen(pstate[i].n) != o[0].nlen || strncasecmp(o[0].name, pstate[i].n, o[0].nlen)) continue;
			int64_t v = o[1].imm;
			if (v < 0 || v > 15) FAIL("immediate must be an integer in range [0, 15].");
			*c->out = 0xd500401fu | (unsigned)pstate[i].op1 << 16 | (unsigned)v << 8 | (unsigned)pstate[i].op2 << 5;
			return 0;
		}
		FAIL("expected writable system register or pstate");
	}
	if (!gpr_nosp(&o[1]) || !isx(&o[1])) BADOPS;
	int r = a64_sysreg(o[0].name, o[0].nlen, 1);
	if (r < 0) FAIL("expected writable system register or pstate");
	*c->out = (m->base & 0xffe7ffffu) | ((unsigned)r & 0xffff) << 5 | R(&o[1]);
	return 0;
}

static int k_mrs(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || !isx(&o[0]) || o[0].sp || o[1].kind != A64O_NAME) BADOPS;
	int r = a64_sysreg(o[1].name, o[1].nlen, 0);
	if (r < 0) FAIL("expected readable system register");
	*c->out = (m->base & 0xffe7ffffu) | ((unsigned)r & 0xffff) << 5 | R(&o[0]);
	return 0;
}

typedef struct { const char *n; uint8_t op1, crn, crm, op2, needreg; } SysOp;

static const SysOp dcops[] = {
	{ "zva", 3, 7, 4, 1, 1 }, { "ivac", 0, 7, 6, 1, 1 }, { "isw", 0, 7, 6, 2, 1 }, { "cvac", 3, 7, 10, 1, 1 },
	{ "csw", 0, 7, 10, 2, 1 }, { "cvau", 3, 7, 11, 1, 1 }, { "civac", 3, 7, 14, 1, 1 }, { "cisw", 0, 7, 14, 2, 1 },
	{ "cvap", 3, 7, 12, 1, 1 }, { "cvadp", 3, 7, 13, 1, 1 }, { "gva", 3, 7, 4, 3, 1 }, { "gzva", 3, 7, 4, 4, 1 },
};
static const SysOp icops[] = {
	{ "ialluis", 0, 7, 1, 0, 0 }, { "iallu", 0, 7, 5, 0, 0 }, { "ivau", 3, 7, 5, 1, 1 },
};
static const SysOp tlbiops[] = {
	{ "vmalle1is", 0, 8, 3, 0, 0 }, { "vmalle1", 0, 8, 7, 0, 0 }, { "vmalle1os", 0, 8, 1, 0, 0 },
	{ "vae1is", 0, 8, 3, 1, 1 }, { "vae1", 0, 8, 7, 1, 1 }, { "vae1os", 0, 8, 1, 1, 1 },
	{ "aside1is", 0, 8, 3, 2, 1 }, { "aside1", 0, 8, 7, 2, 1 }, { "aside1os", 0, 8, 1, 2, 1 },
	{ "vaae1is", 0, 8, 3, 3, 1 }, { "vaae1", 0, 8, 7, 3, 1 }, { "vaae1os", 0, 8, 1, 3, 1 },
	{ "vale1is", 0, 8, 3, 5, 1 }, { "vale1", 0, 8, 7, 5, 1 }, { "vale1os", 0, 8, 1, 5, 1 },
	{ "vaale1is", 0, 8, 3, 7, 1 }, { "vaale1", 0, 8, 7, 7, 1 }, { "vaale1os", 0, 8, 1, 7, 1 },
	{ "alle2", 4, 8, 7, 0, 0 }, { "alle2is", 4, 8, 3, 0, 0 }, { "alle1", 4, 8, 7, 4, 0 }, { "alle1is", 4, 8, 3, 4, 0 },
	{ "vmalls12e1", 4, 8, 7, 6, 0 }, { "vmalls12e1is", 4, 8, 3, 6, 0 }, { "ipas2e1is", 4, 8, 0, 1, 1 },
	{ "ipas2le1is", 4, 8, 0, 5, 1 }, { "ipas2e1", 4, 8, 4, 1, 1 }, { "vae2is", 4, 8, 3, 1, 1 }, { "vale2is", 4, 8, 3, 5, 1 },
	{ "rvae1is", 0, 8, 2, 1, 1 }, { "rvale1is", 0, 8, 2, 5, 1 }, { "rvaae1is", 0, 8, 2, 3, 1 }, { "rvaale1is", 0, 8, 2, 7, 1 },
	{ "rvae1", 0, 8, 6, 1, 1 }, { "rvale1", 0, 8, 6, 5, 1 }, { "rvaae1", 0, 8, 6, 3, 1 }, { "rvaale1", 0, 8, 6, 7, 1 },
	{ "rvae1os", 0, 8, 5, 1, 1 }, { "rvale1os", 0, 8, 5, 5, 1 },
	{ "ripas2e1is", 4, 8, 0, 2, 1 }, { "ripas2le1is", 4, 8, 0, 6, 1 }, { "ripas2e1", 4, 8, 4, 2, 1 },
	{ "ripas2le1", 4, 8, 4, 6, 1 }, { "ripas2e1os", 4, 8, 4, 3, 1 }, { "ripas2le1os", 4, 8, 4, 7, 1 },
	{ "ipas2e1os", 4, 8, 4, 0, 1 }, { "ipas2le1os", 4, 8, 4, 4, 1 }, { "ipas2le1", 4, 8, 4, 5, 1 },
	{ "vmalls12e1os", 4, 8, 1, 6, 0 }, { "alle1os", 4, 8, 1, 4, 0 }, { "alle2os", 4, 8, 1, 0, 0 },
	{ "vae2", 4, 8, 7, 1, 1 }, { "vale2", 4, 8, 7, 5, 1 }, { "vae2os", 4, 8, 1, 1, 1 }, { "vale2os", 4, 8, 1, 5, 1 },
	{ "rvaae1os", 0, 8, 5, 3, 1 }, { "rvaale1os", 0, 8, 5, 7, 1 },
};

static int sysop(Ctx *c, const SysOp *t, size_t nt)
{
	const A64Op *o = c->op;
	if (c->n < 1 || c->n > 2 || o[0].kind != A64O_NAME) BADOPS;
	for (size_t i = 0; i < nt; i++) {
		if (strlen(t[i].n) != o[0].nlen || strncasecmp(o[0].name, t[i].n, o[0].nlen)) continue;
		unsigned rt = 31;
		if (t[i].needreg) {
			if (c->n != 2 || !isx(&o[1]) || o[1].sp) FAIL("specified op requires a register");
			rt = R(&o[1]);
		} else if (c->n != 1) FAIL("specified op does not use a register");
		*c->out = 0xd5080000u | (unsigned)t[i].op1 << 16 | (unsigned)t[i].crn << 12 | (unsigned)t[i].crm << 8 | (unsigned)t[i].op2 << 5 | rt;
		return 0;
	}
	FAIL("invalid operand for system instruction");
}

static const SysOp atops[] = {
	{ "s1e1r", 0, 7, 8, 0, 1 }, { "s1e1w", 0, 7, 8, 1, 1 }, { "s1e0r", 0, 7, 8, 2, 1 }, { "s1e0w", 0, 7, 8, 3, 1 },
	{ "s1e1rp", 0, 7, 9, 0, 1 }, { "s1e1wp", 0, 7, 9, 1, 1 }, { "s1e2r", 4, 7, 8, 0, 1 }, { "s1e2w", 4, 7, 8, 1, 1 },
	{ "s12e1r", 4, 7, 8, 4, 1 }, { "s12e1w", 4, 7, 8, 5, 1 }, { "s12e0r", 4, 7, 8, 6, 1 }, { "s12e0w", 4, 7, 8, 7, 1 },
};
static int k_at(Ctx *c, const Mn *m) { (void)m; return sysop(c, atops, sizeof atops / sizeof atops[0]); }
static int k_rdvl(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || !isx(&o[0]) || o[0].sp || !iscimm(&o[1]) || o[1].imm < -32 || o[1].imm > 31) BADOPS;
	*c->out = m->base | ((unsigned)o[1].imm & 0x3f) << 5 | R(&o[0]);
	return 0;
}
/* SVE predicate ops with one Pn.b operand: x = bit position of the register */
static int k_preg(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 1 || !isreg(&o[0]) || o[0].cls != A64_P || o[0].arr != A64_ELB) BADOPS;
	*c->out = m->base | R(&o[0]) << m->x;
	return 0;
}
static int k_dc(Ctx *c, const Mn *m) { (void)m; return sysop(c, dcops, sizeof dcops / sizeof dcops[0]); }
static int k_ic(Ctx *c, const Mn *m) { (void)m; return sysop(c, icops, sizeof icops / sizeof icops[0]); }
static int k_tlbi(Ctx *c, const Mn *m) { (void)m; return sysop(c, tlbiops, sizeof tlbiops / sizeof tlbiops[0]); }

static int cname(const A64Op *o)
{
	if (o->kind != A64O_NAME || o->nlen < 2 || (o->name[0] | 32) != 'c') return -1;
	return cinum(o->name + 1, o->nlen - 1, 15);
}

static int k_sys(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n < 4 || c->n > 5 || !iscimm(&o[0]) || !iscimm(&o[3])) BADOPS;
	int crn = cname(&o[1]), crm = cname(&o[2]);
	if (crn < 0 || crm < 0 || o[0].imm < 0 || o[0].imm > 7 || o[3].imm < 0 || o[3].imm > 7) BADOPS;
	unsigned rt = 31;
	if (c->n == 5) { if (!isx(&o[4]) || o[4].sp) BADOPS; rt = R(&o[4]); }
	*c->out = m->base | (unsigned)o[0].imm << 16 | (unsigned)crn << 12 | (unsigned)crm << 8 | (unsigned)o[3].imm << 5 | rt;
	return 0;
}

/* MOPS */
static int k_cpy(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[0].kind != A64O_MEM || o[0].mode != A64M_PRE || o[0].hasamt ||
	    o[1].kind != A64O_MEM || o[1].mode != A64M_PRE || o[1].hasamt || !isx(&o[2]) || !o[2].wb || o[2].sp) BADOPS;
	unsigned d = o[0].reg, s = o[1].reg, n = o[2].reg;
	if (d == s || d == n || s == n || d == 31 || s == 31 || n == 31) FAIL("invalid CPY instruction registers");
	*c->out = m->base | m->x << 12 | s << 16 | n << 5 | d;
	return 0;
}

static int k_set(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[0].kind != A64O_MEM || o[0].mode != A64M_PRE || o[0].hasamt || !isx(&o[1]) || !o[1].wb ||
	    !isx(&o[2]) || o[1].sp || o[2].sp) BADOPS;
	*c->out = m->base | m->x << 12 | R(&o[2]) << 16 | R(&o[1]) << 5 | o[0].reg;
	return 0;
}

/* ---------------------------------------------------------------- SIMD */

static int k_vrev(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n == 2 && isgpr(&o[0]) && m->x) {
		if (!gpr_nosp(&o[0]) || !gpr_nosp(&o[1])) BADOPS;
		uint32_t b = m->x;
		if ((b == 0xdac00800 || b == 0xdac00c00) && !isx(&o[0])) BADOPS;
		if (b == 0x5ac00400) b |= (uint32_t)isx(&o[0]) << 31;
		*c->out = b | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (c->n != 2 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG) BADOPS;
	int sz = arr_size(o[0].arr);
	if (sz < 0 || sz > 2) BADOPS;
	*c->out = m->base | arr_q(o[0].arr) << 30 | (unsigned)sz << 22 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_dup(Ctx *c, const Mn *m)
{
	(void)m;
	const A64Op *o = c->op;
	if (c->n != 2 || o[0].kind != A64O_VREG) BADOPS;
	int sz = arr_size(o[0].arr);
	if (sz < 0 || sz > 3) BADOPS;
	unsigned imm5 = 1u << sz;
	if (gpr_nosp(&o[1])) {
		*c->out = 0x0e000c00u | arr_q(o[0].arr) << 30 | imm5 << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (o[1].kind == A64O_VELEM) {
		velem_imm5(&o[1], &imm5);
		*c->out = 0x0e000400u | arr_q(o[0].arr) << 30 | imm5 << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	BADOPS;
}

static int k_ext(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 4 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || o[2].kind != A64O_VREG || !iscimm(&o[3])) BADOPS;
	*c->out = m->base | arr_q(o[0].arr) << 30 | R(&o[2]) << 16 | ((unsigned)o[3].imm & 15) << 11 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

/* AdvSIMD three-same, sized; x=1: no .2d (umin, umax) */
static int k_vperm(Ctx *c, const Mn *m)
{
	if (m->x && c->n == 3 && c->op[0].kind == A64O_VREG && c->op[0].arr == A64_2D) BADOPS;
	return vec3(c, m->base, 1);
}

static int k_pmull(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || o[2].kind != A64O_VREG) BADOPS;
	unsigned sz;
	if (o[0].arr == A64_1Q) sz = 3;
	else if (o[0].arr == A64_8H) sz = 0;
	else BADOPS;
	*c->out = m->base | sz << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_vshift(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || !iscimm(&o[2])) BADOPS;
	int sz = arr_size(o[0].arr);
	if (sz < 0 || sz > 3) BADOPS;
	unsigned esize = 8u << sz, sh = (unsigned)o[2].imm, immhb;
	if (m->x) {	/* right shifts: 1..esize */
		if (sh < 1 || sh > esize) FAIL("immediate out of range");
		immhb = 2 * esize - sh;
	} else {
		if (sh >= esize) FAIL("immediate out of range");
		immhb = esize + sh;
	}
	*c->out = m->base | arr_q(o[0].arr) << 30 | immhb << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_movi(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (!m->x && c->n == 2 && isreg(&o[0]) && o[0].cls == A64_D && iscimm(&o[1])) {	/* movi Dd, #bytemask */
		uint64_t v = (uint64_t)o[1].imm, abc = 0;
		for (int i = 0; i < 8; i++) {
			unsigned b = (unsigned)(v >> 8 * i) & 0xff;
			if (b == 0xff) abc |= 1u << i; else if (b) FAIL("invalid immediate");
		}
		*c->out = 0x2f00e400u | (unsigned)(abc >> 5 & 7) << 16 | (unsigned)(abc & 31) << 5 | R(&o[0]);
		return 0;
	}
	if (c->n < 2 || c->n > 3 || o[0].kind != A64O_VREG || !iscimm(&o[1])) BADOPS;
	if (m->x && (o[0].arr == A64_2D || arr_size(o[0].arr) == 0)) BADOPS;	/* mvni: H and S */
	uint64_t v = (uint64_t)o[1].imm;
	unsigned q = arr_q(o[0].arr), op = m->x, cmode;
	uint64_t abc;
	int sz = arr_size(o[0].arr);
	if (o[0].arr == A64_2D) {
		/* each byte 0x00 or 0xff */
		abc = 0;
		for (int i = 0; i < 8; i++) {
			unsigned b = (unsigned)(v >> 8 * i) & 0xff;
			if (b == 0xff) abc |= 1u << i; else if (b) FAIL("invalid immediate");
		}
		op = 1; cmode = 14;
	} else if (sz == 0) {
		if (v > 0xff) FAIL("immediate out of range");
		abc = v; cmode = 14;
	} else {
		unsigned sh = 0, msl = 0;
		if (c->n == 3) {
			if (o[2].kind != A64O_SHIFT || (o[2].shift != A64_LSL && o[2].shift != A64_MSL)) BADOPS;
			sh = (unsigned)o[2].imm; msl = o[2].shift == A64_MSL;
		}
		if (v > 0xff) FAIL("immediate out of range");
		abc = v;
		if (sz == 1) { if (sh != 0 && sh != 8) BADOPS; cmode = 8 | (sh / 8) << 1; }
		else if (sz == 2) {
			if (msl) { if (sh != 8 && sh != 16) BADOPS; cmode = 12 | (sh == 16); }
			else { if (sh & 7 || sh > 24) BADOPS; cmode = (sh / 8) << 1; }
		} else BADOPS;
	}
	*c->out = 0x0f000400u | q << 30 | op << 29 | (unsigned)(abc >> 5 & 7) << 16 | cmode << 12 | (unsigned)(abc & 31) << 5 | R(&o[0]);
	return 0;
}

/* scalar floating point: ftype S 00, D 01, H 11 at bit 22 */
static int ftype(const A64Op *o)
{
	if (!isreg(o)) return -1;
	return o->cls == A64_S ? 0 : o->cls == A64_D ? 1 : o->cls == A64_H ? 3 : -1;
}

/* fmov #imm: +-(16..31)/16 * 2^(-3..4) as abcdefgh */
static int fp_imm8(double v, unsigned *imm8)
{
	unsigned sgn = v < 0;
	if (sgn) v = -v;
	for (int e = -3; e <= 4; e++)
		for (int f = 16; f < 32; f++)
			if ((double)f / 16 * (e >= 0 ? (double)(1 << e) : 1.0 / (double)(1 << -e)) == v) {
				unsigned ex = (unsigned)(e + 3) ^ 4;	/* exponent bits b:cd with b = NOT(e7) */
				*imm8 = sgn << 7 | (ex & 7) << 4 | (unsigned)(f - 16);
				return 0;
			}
	return -1;
}

static int k_fp(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	int t = c->n ? ftype(&o[0]) : -1;
	uint32_t w;
	switch (m->x) {
	case 1:	/* fadd d, d, d */
		if (c->n != 3 || t < 0 || ftype(&o[1]) != t || ftype(&o[2]) != t) BADOPS;
		*c->out = m->base | (uint32_t)t << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	case 2:	/* fneg d, d */
		if (c->n != 2 || t < 0 || ftype(&o[1]) != t) BADOPS;
		*c->out = m->base | (uint32_t)t << 22 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	case 3:	/* fmadd d, d, d, d */
		if (c->n != 4 || t < 0 || ftype(&o[1]) != t || ftype(&o[2]) != t || ftype(&o[3]) != t) BADOPS;
		*c->out = m->base | (uint32_t)t << 22 | R(&o[2]) << 16 | R(&o[3]) << 10 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	case 4:	/* fcmp d, d | #0.0 */
		if (c->n != 2 || t < 0) BADOPS;
		if (ftype(&o[1]) == t) { *c->out = m->base | (uint32_t)t << 22 | R(&o[1]) << 16 | R(&o[0]) << 5; return 0; }
		if ((o[1].kind == A64O_FIMM && o[1].fimm == 0) || (isimm(&o[1]) && !o[1].hassym && !o[1].imm)) {
			*c->out = m->base | 8 | (uint32_t)t << 22 | R(&o[0]) << 5;
			return 0;
		}
		FAIL("expected floating-point constant #0.0");
	case 5: case 6: {	/* fccmp d, d, #nzcv, cond ; fcsel d, d, d, cond */
		if (c->n != 4 || t < 0 || ftype(&o[1]) != t) BADOPS;
		int cc = getcond(&o[3]);
		if (cc < 0) FAIL("expected AArch64 condition code");
		if (m->x == 5) {
			if (!isimm(&o[2]) || o[2].hassym || o[2].imm < 0 || o[2].imm > 15) BADOPS;
			*c->out = m->base | (uint32_t)t << 22 | R(&o[1]) << 16 | (unsigned)cc << 12 | R(&o[0]) << 5 | (uint32_t)o[2].imm;
		} else {
			if (ftype(&o[2]) != t) BADOPS;
			*c->out = m->base | (uint32_t)t << 22 | R(&o[2]) << 16 | (unsigned)cc << 12 | R(&o[1]) << 5 | R(&o[0]);
		}
		return 0;
	}
	case 7: case 8: {	/* fcvtzs x|w, d ; scvtf d, x|w [, #fbits] */
		const A64Op *g = m->x == 7 ? &o[0] : &o[1], *f = m->x == 7 ? &o[1] : &o[0];
		if (c->n == 2 && t >= 0 && t < 2 && ftype(&o[1]) == t) {	/* AdvSIMD scalar: scvtf d0, d1 */
			static const struct { uint32_t fp; uint32_t simd; } sm[] = {
				{ 0x1e220000, 0x5e21d800 }, { 0x1e230000, 0x7e21d800 }, { 0x1e380000, 0x5ea1b800 }, { 0x1e390000, 0x7ea1b800 },
			};
			for (size_t i = 0; i < sizeof sm / sizeof sm[0]; i++)
				if (sm[i].fp == m->base) { *c->out = sm[i].simd | (uint32_t)t << 22 | R(&o[1]) << 5 | R(&o[0]); return 0; }
			BADOPS;
		}
		if (c->n < 2 || c->n > 3 || !gpr_nosp(g)) BADOPS;
		int ft = ftype(f);
		if (ft < 0) BADOPS;
		w = m->base | (uint32_t)isx(g) << 31 | (uint32_t)ft << 22 | R(&o[1]) << 5 | R(&o[0]);
		if (c->n == 3) {	/* fixed point: bit 21 clear, scale = 64 - fbits */
			int64_t fb = o[2].imm;
			if (!isimm(&o[2]) || o[2].hassym || fb < 1 || fb > (isx(g) ? 64 : 32)) FAIL("invalid fixed-point scale");
			if ((m->base >> 19 & 3) != (m->x == 7 ? 3u : 0u)) BADOPS;
			w &= ~(1u << 21); w = (w & ~(3u << 19)) | (m->x == 7 ? 3u << 19 : 0);
			w |= (uint32_t)(64 - fb) << 10;
		}
		*c->out = w;
		return 0;
	}
	case 9: {	/* fcvt s|d|h, s|d|h */
		int st = c->n == 2 ? ftype(&o[1]) : -1;
		if (c->n != 2 || t < 0 || st < 0 || st == t) BADOPS;
		unsigned opc = t == 0 ? 0 : t == 1 ? 1 : 3;
		*c->out = 0x1e224000u | (uint32_t)st << 22 | opc << 15 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	}
	BADOPS;
}

static int k_fmov(Ctx *c, const Mn *m)
{
	(void)m;
	const A64Op *o = c->op;
	if (c->n == 2 && ftype(&o[0]) >= 0 && (o[1].kind == A64O_FIMM || (isimm(&o[1]) && !o[1].hassym))) {
		double v = o[1].kind == A64O_FIMM ? o[1].fimm : (double)o[1].imm;
		unsigned imm8;
		if (fp_imm8(v, &imm8)) FAIL("expected compatible register or floating-point constant");
		*c->out = 0x1e201000u | (uint32_t)ftype(&o[0]) << 22 | imm8 << 13 | R(&o[0]);
		return 0;
	}
	if (c->n != 2 || !isreg(&o[0]) || !isreg(&o[1])) BADOPS;
	if (o[0].cls == A64_H && (isw(&o[1]) || isx(&o[1]))) { *c->out = 0x1ee70000u | (uint32_t)isx(&o[1]) << 31 | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if ((isw(&o[0]) || isx(&o[0])) && o[1].cls == A64_H) { *c->out = 0x1ee60000u | (uint32_t)isx(&o[0]) << 31 | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if (o[0].cls == A64_H && o[1].cls == A64_H) { *c->out = 0x1ee04000u | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if (o[0].cls == A64_D && isx(&o[1])) { *c->out = 0x9e670000u | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if (isx(&o[0]) && o[1].cls == A64_D) { *c->out = 0x9e660000u | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if (o[0].cls == A64_S && isw(&o[1])) { *c->out = 0x1e270000u | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if (isw(&o[0]) && o[1].cls == A64_S) { *c->out = 0x1e260000u | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if (o[0].cls == A64_D && o[1].cls == A64_D) { *c->out = 0x1e604000u | R(&o[1]) << 5 | R(&o[0]); return 0; }
	if (o[0].cls == A64_S && o[1].cls == A64_S) { *c->out = 0x1e204000u | R(&o[1]) << 5 | R(&o[0]); return 0; }
	BADOPS;
}

/* sha1c/p/m qd, sn, vm.4s ; sha256h qd, qn, vm.4s ; sha1su0/sha256su1 v.4s x3 */
static int k_sha3(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[2].kind != A64O_VREG || o[2].arr != A64_4S) BADOPS;
	if (m->x == 1) { if (o[0].kind != A64O_VREG || o[1].kind != A64O_VREG) BADOPS; }
	else if (m->x == 2) { if (!isreg(&o[0]) || o[0].cls != A64_Q || !isreg(&o[1]) || o[1].cls != A64_Q) BADOPS; }
	else { if (!isreg(&o[0]) || o[0].cls != A64_Q || !isreg(&o[1]) || o[1].cls != A64_S) BADOPS; }
	*c->out = m->base | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

static int k_sha2(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2) BADOPS;
	if (m->x) { if (o[0].kind != A64O_VREG || o[1].kind != A64O_VREG) BADOPS; }
	else if (!isreg(&o[0]) || o[0].cls != A64_S || !isreg(&o[1]) || o[1].cls != A64_S) BADOPS;
	*c->out = m->base | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}


/* ---------------------------------------------------------------- atomics (parsed, not tabled) */

static int mem0(const A64Op *o) { return o->kind == A64O_MEM && o->mode == A64M_OFF && !o->imm && !o->hassym; }

/* size suffix b/h at s[0..n): -1 none, else size; *rest = chars consumed */
static int sfx(const char *s, size_t n, size_t *k)
{
	if (*k < n && (s[*k] | 32) == 'b') { (*k)++; return 0; }
	if (*k < n && (s[*k] | 32) == 'h') { (*k)++; return 1; }
	return -1;
}

static int pre(const char *s, size_t n, size_t *k, const char *p)
{
	size_t l = strlen(p);
	if (*k + l > n || lc_cmp(s + *k, l, p)) return 0;
	*k += l;
	return 1;
}

/* LSE (cas, ld<op>, st<op>, swp), load-acquire/store-release and exclusives.
 * Returns 1 if mn is one of them (result in *r), 0 otherwise. */
static int k_atomic(Ctx *c, const char *mn, size_t n, int *r)
{
	static const char *const ops[] = { "add", "clr", "eor", "set", "smax", "smin", "umax", "umin" };
	const A64Op *o = c->op;
	size_t k = 0;
	int a = 0, l = 0, sz, cls = -1;	/* cls: 0 cas 1 ld<op> 2 st<op> 3 swp 4 ldar 5 stlr 6 ldapr 7 ldxr 8 ldaxr 9 stxr 10 stlxr */
	unsigned opc = 0;

	if (pre(mn, n, &k, "ldaxp")) cls = 12;
	else if (pre(mn, n, &k, "ldxp")) cls = 13;
	else if (pre(mn, n, &k, "stlxp")) cls = 14;
	else if (pre(mn, n, &k, "stxp")) cls = 15;
	else if (pre(mn, n, &k, "casp")) cls = 11;
	else if (pre(mn, n, &k, "cas")) cls = 0;
	else if (pre(mn, n, &k, "swp")) cls = 3;
	else if (pre(mn, n, &k, "ldapr")) cls = 6;
	else if (pre(mn, n, &k, "ldaxr")) cls = 8;
	else if (pre(mn, n, &k, "ldar")) cls = 4;
	else if (pre(mn, n, &k, "ldxr")) cls = 7;
	else if (pre(mn, n, &k, "stlxr")) cls = 10;
	else if (pre(mn, n, &k, "stlr")) cls = 5;
	else if (pre(mn, n, &k, "stxr")) cls = 9;
	else if (n > 2 && (pre(mn, n, &k, "ld") || pre(mn, n, &k, "st"))) {
		int st = (mn[0] | 32) == 's';
		for (opc = 0; opc < 8; opc++) {
			size_t k2 = k;
			if (pre(mn, n, &k2, ops[opc])) { k = k2; break; }
		}
		if (opc == 8) return 0;
		cls = st ? 2 : 1;
	} else
		return 0;
	if (cls <= 3 || cls == 11) {
		if (cls != 2 && k < n && (mn[k] | 32) == 'a') { a = 1; k++; }
		if (k < n && (mn[k] | 32) == 'l') { l = 1; k++; }
	}
	sz = sfx(mn, n, &k);
	if (k != n) return 0;
	*r = -1;
	*c->err = "invalid operand for instruction";
	switch (cls) {
	case 0: case 1: case 3:	/* Rs, Rt, [Xn] */
		if (c->n != 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || !mem0(&o[2])) return 1;
		if (sz < 0) sz = isx(&o[1]) ? 3 : 2;
		else if (!isw(&o[0]) || !isw(&o[1])) return 1;
		if (isx(&o[0]) != isx(&o[1])) return 1;
		if (cls == 0)
			*c->out = 0x08a07c00u | (uint32_t)sz << 30 | (uint32_t)a << 22 | R(&o[0]) << 16 | (uint32_t)l << 15 | (unsigned)o[2].reg << 5 | R(&o[1]);
		else
			*c->out = 0x38200000u | (uint32_t)sz << 30 | (uint32_t)a << 23 | (uint32_t)l << 22 | R(&o[0]) << 16 |
				  (cls == 3 ? 0x8000u : opc << 12) | (unsigned)o[2].reg << 5 | R(&o[1]);
		break;
	case 2:			/* st<op>: Rs, [Xn] = ld<op> with Rt = zr */
		if (c->n != 2 || !gpr_nosp(&o[0]) || !mem0(&o[1])) return 1;
		if (sz < 0) sz = isx(&o[0]) ? 3 : 2;
		else if (!isw(&o[0])) return 1;
		*c->out = 0x3820001fu | (uint32_t)sz << 30 | (uint32_t)l << 22 | R(&o[0]) << 16 | opc << 12 | (unsigned)o[1].reg << 5;
		break;
	case 4: case 5: case 6: case 7: case 8: {	/* Rt, [Xn] */
		static const uint32_t base[] = { 0, 0, 0, 0, 0x08dffc00, 0x089ffc00, 0x38bfc000, 0x085f7c00, 0x085ffc00 };
		if (c->n != 2 || !gpr_nosp(&o[0]) || !mem0(&o[1])) return 1;
		if (sz < 0) sz = isx(&o[0]) ? 3 : 2;
		else if (!isw(&o[0])) return 1;
		*c->out = base[cls] | (uint32_t)sz << 30 | (unsigned)o[1].reg << 5 | R(&o[0]);
		break;
	}
	case 11:		/* casp: Rs, Rs+1, Rt, Rt+1, [Xn] */
		if (sz >= 0 || c->n != 5 || !mem0(&o[4])) return 1;
		for (int i = 0; i < 4; i++)
			if (!gpr_nosp(&o[i]) || isx(&o[i]) != isx(&o[0])) return 1;
		if ((R(&o[0]) & 1) || R(&o[1]) != R(&o[0]) + 1 || (R(&o[2]) & 1) || R(&o[3]) != R(&o[2]) + 1) return 1;
		*c->out = 0x08207c00u | (uint32_t)isx(&o[0]) << 30 | (uint32_t)a << 22 | R(&o[0]) << 16 | (uint32_t)l << 15 | (unsigned)o[4].reg << 5 | R(&o[2]);
		break;
	case 12: case 13:	/* ldxp Rt, Rt2, [Xn] */
		if (sz >= 0 || c->n != 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || isx(&o[0]) != isx(&o[1]) || !mem0(&o[2])) return 1;
		*c->out = 0x887f0000u | (uint32_t)isx(&o[0]) << 30 | (cls == 12 ? 0x8000u : 0) | R(&o[1]) << 10 | (unsigned)o[2].reg << 5 | R(&o[0]);
		break;
	case 14: case 15:	/* stxp Ws, Rt, Rt2, [Xn] */
		if (sz >= 0 || c->n != 4 || !isw(&o[0]) || !gpr_nosp(&o[1]) || !gpr_nosp(&o[2]) || isx(&o[1]) != isx(&o[2]) || !mem0(&o[3])) return 1;
		*c->out = 0x88200000u | (uint32_t)isx(&o[1]) << 30 | R(&o[0]) << 16 | (cls == 14 ? 0x8000u : 0) | R(&o[2]) << 10 | (unsigned)o[3].reg << 5 | R(&o[1]);
		break;
	default:		/* stxr/stlxr: Ws, Rt, [Xn] */
		if (c->n != 3 || !isw(&o[0]) || !gpr_nosp(&o[1]) || !mem0(&o[2])) return 1;
		if (sz < 0) sz = isx(&o[1]) ? 3 : 2;
		else if (!isw(&o[1])) return 1;
		*c->out = (cls == 10 ? 0x0800fc00u : 0x08007c00u) | (uint32_t)sz << 30 | R(&o[0]) << 16 | (unsigned)o[2].reg << 5 | R(&o[1]);
		break;
	}
	*c->err = 0;
	*r = 0;
	return 1;
}

/* smov Wd|Xd, Vn.T[i] (T: b, h; s only into Xd) */
static int k_smov(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	unsigned imm5;
	if (c->n != 2 || !gpr_nosp(&o[0]) || o[1].kind != A64O_VELEM) BADOPS;
	int sz = velem_imm5(&o[1], &imm5);
	if (sz < 0 || sz > 2 || (sz == 2 && !isx(&o[0]))) BADOPS;
	*c->out = m->base | (uint32_t)isx(&o[0]) << 30 | imm5 << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

/* across lanes: sminv/smaxv/uminv/umaxv/addv Bd|Hd|Sd, Vn.T */
static int k_across(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	static const uint8_t cls_of[3] = { A64_B, A64_H, A64_S };
	if (c->n != 2 || !isreg(&o[0]) || o[1].kind != A64O_VREG) BADOPS;
	int sz = arr_size(o[1].arr);
	static const uint8_t cls_long[3] = { A64_H, A64_S, A64_D };
	if (sz < 0 || sz > 2 || (sz == 2 && !arr_q(o[1].arr)) || o[0].cls != (m->x ? cls_long : cls_of)[sz]) BADOPS;
	*c->out = m->base | arr_q(o[1].arr) << 30 | (uint32_t)sz << 22 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

/*
 * AdvSIMD two-register misc, size from the source: cnt Vd.T, Vn.T (8b/16b);
 * x=1, long pairwise (uaddlp, uadalp): Vd has elements twice Vn's, same Q.
 */
static int k_vmisc(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG) BADOPS;
	int sz = arr_size(o[1].arr);
	if (!m->x) {
		if (o[0].arr != o[1].arr || (o[1].arr != A64_8B && o[1].arr != A64_16B)) BADOPS;
	} else if (sz < 0 || sz > 2 || arr_size(o[0].arr) != sz + 1 || arr_q(o[0].arr) != arr_q(o[1].arr)) {
		BADOPS;
	}
	*c->out = m->base | arr_q(o[1].arr) << 30 | (uint32_t)sz << 22 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}
/* pointer authentication, hints with operands, RCpc unscaled, MTE tags:
 * x is the operand shape (see the cases) */
static int pac_x(const A64Op *o, int sp) { return isx(o) && (sp || !o->sp); }
static int pac_mem9(Ctx *c, const A64Op *o)	/* [Xn|sp{, #simm9}] */
{
	if (o->kind != A64O_MEM || o->mode != A64M_OFF || o->hassym) return -1;
	if (o->imm < -256 || o->imm > 255) { *c->err = "index must be an integer in range [-256, 255]."; return -2; }
	return 0;
}
static int k_pac(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	uint32_t b = m->base;
	int r;

	switch (m->x) {
	case 1:	/* Xd */
		if (c->n != 1 || !pac_x(&o[0], 0)) BADOPS;
		*c->out = b | R(&o[0]);
		return 0;
	case 2:	/* Xn */
		if (c->n != 1 || !pac_x(&o[0], 0)) BADOPS;
		*c->out = b | R(&o[0]) << 5;
		return 0;
	case 3:	/* Xd, Xn|sp */
		if (c->n != 2 || !pac_x(&o[0], 0) || !pac_x(&o[1], 1)) BADOPS;
		*c->out = b | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	case 4:	/* Xn, Xm|sp */
		if (c->n != 2 || !pac_x(&o[0], 0) || !pac_x(&o[1], 1)) BADOPS;
		*c->out = b | R(&o[0]) << 5 | R(&o[1]);
		return 0;
	case 5:	/* Xd, Xn, Xm (sp where the instruction takes it) */
		if (c->n != 3 || !pac_x(&o[0], 0) || !pac_x(&o[1], 1) || !pac_x(&o[2], 1)) BADOPS;
		*c->out = b | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	case 6: {	/* ldraa Xt, [Xn|sp{, #simm10*8}]{!} */
		if (c->n != 2 || !pac_x(&o[0], 0) || o[1].kind != A64O_MEM || o[1].hassym ||
		    (o[1].mode != A64M_OFF && o[1].mode != A64M_PRE)) BADOPS;
		int64_t v = o[1].imm;
		if (v & 7 || v < -4096 || v > 4088) FAIL("index must be a multiple of 8 in range [-4096, 4088].");
		unsigned u = (unsigned)(v / 8) & 0x3ff;
		*c->out = b | (u >> 9) << 22 | (u & 0x1ff) << 12 | (o[1].mode == A64M_PRE) << 11 | (unsigned)o[1].reg << 5 | R(&o[0]);
		return 0;
	}
	case 7:	/* psb/tsb csync */
		if (c->n != 1 || o[0].kind != A64O_NAME || o[0].nlen != 5 || strncasecmp(o[0].name, "csync", 5)) BADOPS;
		*c->out = b;
		return 0;
	case 8:	/* chkfeat x16 */
		if (c->n != 1 || !pac_x(&o[0], 0) || R(&o[0]) != 16) BADOPS;
		*c->out = b;
		return 0;
	case 10: {	/* sysl Xt, #op1, Cn, Cm, #op2 */
		if (c->n != 5 || !pac_x(&o[0], 0) || !iscimm(&o[1]) || !iscimm(&o[4])) BADOPS;
		int crn = cname(&o[2]), crm = cname(&o[3]);
		if (crn < 0 || crm < 0 || o[1].imm < 0 || o[1].imm > 7 || o[4].imm < 0 || o[4].imm > 7) BADOPS;
		*c->out = b | (unsigned)o[1].imm << 16 | (unsigned)crn << 12 | (unsigned)crm << 8 | (unsigned)o[4].imm << 5 | R(&o[0]);
		return 0;
	}
	case 11: case 12: case 13: case 14:	/* ldapur*, stlur*: 11 W, 14 X, 12 W|X (X: size 3), 13 X|W (W: opc 11) */
		if (c->n != 2 || !isgpr(&o[0]) || o[0].sp) BADOPS;
		if ((m->x == 11 && !isw(&o[0])) || (m->x == 14 && !isx(&o[0]))) BADOPS;
		if (m->x == 12 && isx(&o[0])) b |= 0x40000000;
		if (m->x == 13 && isw(&o[0])) b |= 0x00400000;
		if ((r = pac_mem9(c, &o[1])) == -1) BADOPS;
		if (r < 0) return -1;
		*c->out = b | ((unsigned)o[1].imm & 0x1ff) << 12 | (unsigned)o[1].reg << 5 | R(&o[0]);
		return 0;
	case 15: {	/* prfum op, [Xn|sp{, #simm9}] */
		int op;
		if (c->n != 2) BADOPS;
		if (o[0].kind == A64O_NAME) op = prfop(o[0].name, o[0].nlen);
		else if (iscimm(&o[0])) op = (int)o[0].imm;
		else BADOPS;
		if (op < 0 || op > 31) FAIL("prefetch hint expected");
		if ((r = pac_mem9(c, &o[1])) == -1) BADOPS;
		if (r < 0) return -1;
		*c->out = b | ((unsigned)o[1].imm & 0x1ff) << 12 | (unsigned)o[1].reg << 5 | (unsigned)op;
		return 0;
	}
	case 16:	/* irg Xd|sp, Xn|sp{, Xm} */
		if (c->n < 2 || c->n > 3 || !pac_x(&o[0], 1) || !pac_x(&o[1], 1) || (c->n == 3 && !pac_x(&o[2], 0))) BADOPS;
		*c->out = b | (c->n == 3 ? R(&o[2]) : 31) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	case 17: {	/* addg/subg Xd|sp, Xn|sp, #uimm6*16, #uimm4 */
		if (c->n != 4 || !pac_x(&o[0], 1) || !pac_x(&o[1], 1) || !iscimm(&o[2]) || !iscimm(&o[3])) BADOPS;
		if (o[2].imm & 15 || o[2].imm < 0 || o[2].imm > 1008) FAIL("index must be a multiple of 16 in range [0, 1008].");
		if (o[3].imm < 0 || o[3].imm > 15) FAIL("immediate must be an integer in range [0, 15].");
		*c->out = b | (unsigned)(o[2].imm / 16) << 16 | (unsigned)o[3].imm << 10 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case 20:	/* cmpp Xn|sp, Xm|sp */
		if (c->n != 2 || !pac_x(&o[0], 1) || !pac_x(&o[1], 1)) BADOPS;
		*c->out = b | R(&o[1]) << 16 | R(&o[0]) << 5;
		return 0;
	case 21: {	/* stgp Xt1, Xt2, [Xn|sp{, #imm7*16}]{!} | [Xn|sp], #imm7*16 */
		if (c->n < 3 || c->n > 4 || !pac_x(&o[0], 0) || !pac_x(&o[1], 0) || o[2].kind != A64O_MEM || o[2].hassym) BADOPS;
		int64_t v = o[2].imm;
		uint32_t mode = 0x01000000;	/* signed offset */
		if (c->n == 4) {
			if (o[2].mode != A64M_OFF || v || !iscimm(&o[3])) BADOPS;
			v = o[3].imm, mode = 0x00800000;
		} else if (o[2].mode == A64M_PRE)
			mode = 0x01800000;
		else if (o[2].mode != A64M_OFF)
			BADOPS;
		if (v & 15 || v < -1024 || v > 1008) FAIL("index must be a multiple of 16 in range [-1024, 1008].");
		*c->out = 0x68000000 | mode | ((unsigned)(v / 16) & 0x7f) << 15 | R(&o[1]) << 10 | (unsigned)o[2].reg << 5 | R(&o[0]);
		return 0;
	}
	}
	BADOPS;
}
/* integer aliases: x 0x07/0x0f/0x1f sxtb..uxth (base: SBFM 0, UBFM 2), 2 ngc(s),
 * 3 CSSC abs/cnt/ctz (vector cnt: VMISC), 4 CSSC smax.. (vector umax/umin: VPERM), 5 bfc */
static int k_ialias(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;

	if (c->n >= 2 && o[0].kind == A64O_VREG) {
		static const Mn vcnt = { "cnt", K_VMISC, 0x0e205800, 0 }, vumax = { "umax", K_VPERM, 0x2e206400, 1 },
				vumin = { "umin", K_VPERM, 0x2e206c00, 1 };

		if (m->x == 3 && m->base == 0x5ac01c00) return k_vmisc(c, &vcnt);
		if (m->x == 4 && m->base == 0x1ac06400) return k_vperm(c, &vumax);
		if (m->x == 4 && m->base == 0x1ac06c00) return k_vperm(c, &vumin);
		BADOPS;
	}
	if (m->x >= 0x07) {	/* sxtb Wd|Xd, Wn; uxtb Wd, Wn; sxtw Xd, Wn */
		if (c->n != 2 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || !isw(&o[1])) BADOPS;
		int sf = isx(&o[0]);
		if (m->x == 0x1f && !sf) BADOPS;
		if (m->base == 2 && m->x != 0x1f)
			sf = 0;	/* uxtb/uxth Xd: the W form (llvm-mc) */
		*c->out = a64_bfm(sf, m->base, R(&o[0]), R(&o[1]), 0, m->x);
		return 0;
	}
	switch (m->x) {
	case 2:	/* ngc Rd, Rm = sbc Rd, zr, Rm */
		if (c->n != 2 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || isx(&o[0]) != isx(&o[1])) BADOPS;
		*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | R(&o[1]) << 16 | R(&o[0]);
		return 0;
	case 3:
		if (c->n != 2 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || isx(&o[0]) != isx(&o[1])) BADOPS;
		*c->out = m->base | (uint32_t)isx(&o[0]) << 31 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	case 4: {
		if (c->n != 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1])) BADOPS;
		int sf = isx(&o[0]);
		if (isx(&o[1]) != sf) BADOPS;
		if (iscimm(&o[2])) {	/* imm8: signed for smax/smin, unsigned for umax/umin */
			int u = (m->base >> 10) & 1;
			int64_t v = o[2].imm;
			if (u ? v < 0 || v > 255 : v < -128 || v > 127) FAIL("immediate must be an integer in range");
			*c->out = 0x11c00000 | (uint32_t)sf << 31 | ((m->base >> 10) & 3) << 18 | ((uint32_t)v & 255) << 10 |
				  R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		if (!gpr_nosp(&o[2]) || isx(&o[2]) != sf) BADOPS;
		*c->out = m->base | (uint32_t)sf << 31 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case 5: {	/* bfc Rd, #lsb, #width = bfm Rd, zr, #(-lsb % size), #(width - 1) */
		if (c->n != 3 || !gpr_nosp(&o[0]) || !iscimm(&o[1]) || !iscimm(&o[2])) BADOPS;
		int sf = isx(&o[0]);
		unsigned size = sf ? 64 : 32, lsb = (unsigned)o[1].imm, w = (unsigned)o[2].imm;
		if (lsb >= size || !w || lsb + w > size) FAIL("requested insert overflows register");
		*c->out = a64_bfm(sf, 1, R(&o[0]), 31, (size - lsb) & (size - 1), w - 1);
		return 0;
	}
	}
	BADOPS;
}

/* addp: Vd.T, Vn.T, Vm.T, or the scalar Dd, Vn.2d */
static int k_addp(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n == 2 && isreg(&o[0]) && o[0].cls == A64_D && o[1].kind == A64O_VREG && o[1].arr == A64_2D) {
		*c->out = 0x5ef1b800u | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (c->n == 3 && o[0].kind == A64O_VREG && o[0].arr == A64_1D) BADOPS;
	return vec3(c, m->base, 1);
}

/* not Vd.8b/16b, Vn.T (mvn's vector form) */
static int k_vnot(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || (o[0].arr != A64_8B && o[0].arr != A64_16B) || o[1].arr != o[0].arr) BADOPS;
	*c->out = m->base | arr_q(o[0].arr) << 30 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

/* AdvSIMD two-register (aese/aesd/aesmc/aesimc: .16b only) */
static int k_v2(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || o[0].arr != A64_16B || o[1].arr != A64_16B) BADOPS;
	*c->out = m->base | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

/* AdvSIMD three-same without size (bsl/bit/bif: .8b/.16b) or byte-only sized (pmul) */
static int k_v3(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || o[2].kind != A64O_VREG) BADOPS;
	if (o[0].arr != A64_8B && o[0].arr != A64_16B) BADOPS;
	if (o[1].arr != o[0].arr || o[2].arr != o[0].arr) BADOPS;
	*c->out = m->base | arr_q(o[0].arr) << 30 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

/* tbl/tbx Vd.T, {Vn.16b, ...}, Vm.T */
static int k_tbl(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 3 || o[0].kind != A64O_VREG || o[1].kind != A64O_LIST || o[2].kind != A64O_VREG) BADOPS;
	if ((o[0].arr != A64_8B && o[0].arr != A64_16B) || o[2].arr != o[0].arr || o[1].arr != A64_16B || o[1].hasidx) BADOPS;
	if (o[1].nregs < 1 || o[1].nregs > 4) BADOPS;
	*c->out = m->base | arr_q(o[0].arr) << 30 | R(&o[2]) << 16 | (unsigned)(o[1].nregs - 1) << 13 | (unsigned)o[1].reg << 5 | R(&o[0]);
	return 0;
}

/* sha512h/h2 Qd, Qn, Vm.2d; sha512su0 Vd.2d, Vn.2d; sha512su1 Vd.2d, Vn.2d, Vm.2d */
static int k_sha512(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (m->x == 3) {	/* eor3/bcax Vd.16b, Vn.16b, Vm.16b, Va.16b */
		if (c->n != 4) BADOPS;
		for (int i = 0; i < 4; i++)
			if (o[i].kind != A64O_VREG || o[i].arr != A64_16B) BADOPS;
		*c->out = m->base | R(&o[2]) << 16 | R(&o[3]) << 10 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (m->x == 1) {
		if (c->n != 2 || o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || o[0].arr != A64_2D || o[1].arr != A64_2D) BADOPS;
		*c->out = m->base | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	if (c->n != 3 || o[2].kind != A64O_VREG || o[2].arr != A64_2D) BADOPS;
	if (m->x == 2) { if (o[0].kind != A64O_VREG || o[1].kind != A64O_VREG || o[0].arr != A64_2D || o[1].arr != A64_2D) BADOPS; }
	else if (!isreg(&o[0]) || o[0].cls != A64_Q || !isreg(&o[1]) || o[1].cls != A64_Q) BADOPS;
	*c->out = m->base | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
	return 0;
}

/* ld1r..ld4r {Vt.T, ...}, [Xn] / [Xn], #imm / [Xn], Xm */
static int k_ldr_rep(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n < 2 || c->n > 3 || o[0].kind != A64O_LIST || o[0].hasidx || o[1].kind != A64O_MEM || o[1].mode != A64M_OFF || o[1].hasamt || o[1].imm) BADOPS;
	unsigned nr = o[0].nregs;
	int sz = arr_size(o[0].arr);
	if (nr != m->x || sz < 0 || sz > 3) BADOPS;
	uint32_t w = m->base | arr_q(o[0].arr) << 30 | (unsigned)sz << 10 | (unsigned)o[1].reg << 5 | R(&o[0]);
	if (c->n == 3) {
		w |= 0x00800000;
		if (iscimm(&o[2])) {
			if (o[2].imm != (int64_t)nr << sz) FAIL("invalid post-increment");
			w |= 31u << 16;
		} else if (gpr_nosp(&o[2]) && isx(&o[2]) && o[2].reg != 31) w |= R(&o[2]) << 16;
		else BADOPS;
	}
	*c->out = w;
	return 0;
}

/* ins Vd.T[i], Rn / ins Vd.T[i], Vn.T[j]: the mov aliases */
static int k_ins(Ctx *c, const Mn *m)
{
	if (c->n != 2 || c->op[0].kind != A64O_VELEM) BADOPS;
	return k_mov(c, m);
}

/* MTE: stg/stzg/st2g/stz2g Xt|sp, [Xn|sp{, #simm}]{!} / [Xn], #simm; ldg Xt, [Xn{, #simm}] */
static int k_mte(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n < 2 || c->n > 3 || !isx(&o[0]) || o[1].kind != A64O_MEM || o[1].mode == A64M_REG || o[1].hassym) BADOPS;
	int64_t off = o[1].imm;
	unsigned mode = 2;	/* signed offset */
	if (c->n == 3) {
		if (o[1].mode != A64M_OFF || off || !iscimm(&o[2])) BADOPS;
		off = o[2].imm, mode = 1;
	} else if (o[1].mode == A64M_PRE)
		mode = 3;
	if (off & 15 || off < -4096 || off > 4080) FAIL("index must be a multiple of 16 in range [-4096, 4080]");
	if (m->x == 1 && mode != 2) BADOPS;	/* ldg: offset form only */
	*c->out = m->base | ((unsigned)(off / 16) & 0x1ff) << 12 | (m->x == 1 ? 0 : mode << 10) | (unsigned)o[1].reg << 5 | R(&o[0]);
	return 0;
}

/* stgm/stzgm/ldgm Xt, [Xn|sp] */
static int k_mtem(Ctx *c, const Mn *m)
{
	const A64Op *o = c->op;
	if (c->n != 2 || !isx(&o[0]) || o[0].sp || o[1].kind != A64O_MEM || o[1].mode != A64M_OFF || o[1].imm || o[1].hassym) BADOPS;
	*c->out = m->base | (unsigned)o[1].reg << 5 | R(&o[0]);
	return 0;
}

/* FEAT_LSUI: cas{a}{l}t, swp{a}{l}t, ldt<add|clr|set>{a}{l}, stt<add|clr|set>{l} */
static int k_lsui(Ctx *c, const char *mn, size_t n, int *r)
{
	static const char *const ops[] = { "add", "clr", 0, "set" };
	const A64Op *o = c->op;
	size_t k = 0;
	int cls = -1, a = 0, l = 0;
	unsigned opc = 0;

	if (pre(mn, n, &k, "cas")) cls = 0;
	else if (pre(mn, n, &k, "swpt")) cls = 1;	/* swpt{a}{l}: the t comes first */
	else if (pre(mn, n, &k, "ldt") || pre(mn, n, &k, "stt")) {
		cls = (mn[0] | 32) == 's' ? 3 : 2;
		for (opc = 0; opc < 4; opc++) {
			size_t k2 = k;
			if (ops[opc] && pre(mn, n, &k2, ops[opc])) { k = k2; break; }
		}
		if (opc == 4) return 0;
	} else
		return 0;
	if (cls != 3 && k < n && (mn[k] | 32) == 'a') { a = 1; k++; }
	if (k < n && (mn[k] | 32) == 'l') { l = 1; k++; }
	if (cls == 0) {
		if (k + 1 != n || (mn[k] | 32) != 't') return 0;
	} else if (k != n)
		return 0;
	*r = -1;
	*c->err = "invalid operand for instruction";
	if (cls == 3) {		/* stt<op> Rs, [Xn] = ldt<op> with Rt = zr */
		if (c->n != 2 || !gpr_nosp(&o[0]) || !mem0(&o[1])) return 1;
		*c->out = 0x1920041fu | (uint32_t)isx(&o[0]) << 30 | (uint32_t)l << 22 | R(&o[0]) << 16 | opc << 12 | (unsigned)o[1].reg << 5;
	} else {
		if (c->n != 3 || !gpr_nosp(&o[0]) || !gpr_nosp(&o[1]) || isx(&o[0]) != isx(&o[1]) || !mem0(&o[2])) return 1;
		if (cls == 0)
			*c->out = 0x89807c00u | (uint32_t)isx(&o[0]) << 30 | (uint32_t)a << 22 | R(&o[0]) << 16 | (uint32_t)l << 15 | (unsigned)o[2].reg << 5 | R(&o[1]);
		else
			*c->out = (cls == 1 ? 0x19208400u : 0x19200400u | opc << 12) | (uint32_t)isx(&o[0]) << 30 | (uint32_t)a << 23 | (uint32_t)l << 22 |
				  R(&o[0]) << 16 | (unsigned)o[2].reg << 5 | R(&o[1]);
	}
	*c->err = 0;
	*r = 0;
	return 1;
}

/* ---------------------------------------------------------------- AdvSIMD vector forms
 * Mnemonics with a vector form looked up when the first operand is a
 * vector (some share a name with a scalar instruction: mul, neg, fadd).
 * Returns 1 when the operands are not this form (the normal table goes on). */
enum { VS_SAME = 1, VS_FSAME, VS_MISC, VS_FMISC, VS_CMZ, VS_LONG, VS_SHLL, VS_XTN, VS_SHRN, VS_FMOV };
typedef struct { const char *name; uint8_t sub, fl; uint32_t base, b2, b3; } Vf;
enum { VF_NO2D = 1, VF_NOB = 2, VF_WIDE = 4, VF_BHS = 8 };
static const Vf vtab[] = {	/* sorted by name */
	{ "abs", VS_MISC, 0, 0x0e20b800, 0, 0 },
	{ "cmeq", VS_CMZ, 0, 0x0e209800, 0, 0 },
	{ "cmge", VS_CMZ, 0, 0x2e208800, 0, 0 },
	{ "cmgt", VS_CMZ, 0, 0x0e208800, 0, 0 },
	{ "cmle", VS_CMZ, 0, 0x2e209800, 0, 0 },
	{ "cmlt", VS_CMZ, 0, 0x0e20a800, 0, 0 },
	{ "fabd", VS_FSAME, 0, 0x2ea0d400, 0, 0 },
	{ "fabs", VS_FMISC, 0, 0x0ea0f800, 0, 0 },
	{ "facge", VS_FSAME, 0, 0x2e20ec00, 0, 0 },
	{ "facgt", VS_FSAME, 0, 0x2ea0ec00, 0, 0 },
	{ "fadd", VS_FSAME, 0, 0x0e20d400, 0, 0 },
	{ "faddp", VS_FSAME, 0, 0x2e20d400, 0, 0 },
	{ "fcmeq", VS_FSAME, 0, 0x0e20e400, 0, 0x0ea0d800 },
	{ "fcmge", VS_FSAME, 0, 0x2e20e400, 0, 0x2ea0c800 },
	{ "fcmgt", VS_FSAME, 0, 0x2ea0e400, 0, 0x0ea0c800 },
	{ "fcmle", VS_FSAME, 0, 0, 0, 0x2ea0d800 },
	{ "fcmlt", VS_FSAME, 0, 0, 0, 0x0ea0e800 },
	{ "fcvtzs", VS_FMISC, 0, 0x0ea1b800, 0, 0 },
	{ "fcvtzu", VS_FMISC, 0, 0x2ea1b800, 0, 0 },
	{ "fdiv", VS_FSAME, 0, 0x2e20fc00, 0, 0 },
	{ "fmax", VS_FSAME, 0, 0x0e20f400, 0, 0 },
	{ "fmaxnm", VS_FSAME, 0, 0x0e20c400, 0, 0 },
	{ "fmaxnmp", VS_FSAME, 0, 0x2e20c400, 0, 0 },
	{ "fmaxp", VS_FSAME, 0, 0x2e20f400, 0, 0 },
	{ "fmin", VS_FSAME, 0, 0x0ea0f400, 0, 0 },
	{ "fminnm", VS_FSAME, 0, 0x0ea0c400, 0, 0 },
	{ "fminnmp", VS_FSAME, 0, 0x2ea0c400, 0, 0 },
	{ "fminp", VS_FSAME, 0, 0x2ea0f400, 0, 0 },
	{ "fmla", VS_FSAME, 0, 0x0e20cc00, 0x0f801000, 0 },
	{ "fmls", VS_FSAME, 0, 0x0ea0cc00, 0x0f805000, 0 },
	{ "fmov", VS_FMOV, 0, 0x0f00f400, 0, 0 },
	{ "fmul", VS_FSAME, 0, 0x2e20dc00, 0x0f809000, 0 },
	{ "fneg", VS_FMISC, 0, 0x2ea0f800, 0, 0 },
	{ "frinta", VS_FMISC, 0, 0x2e218800, 0, 0 },
	{ "frinti", VS_FMISC, 0, 0x2ea19800, 0, 0 },
	{ "frintm", VS_FMISC, 0, 0x0e219800, 0, 0 },
	{ "frintn", VS_FMISC, 0, 0x0e218800, 0, 0 },
	{ "frintp", VS_FMISC, 0, 0x0ea18800, 0, 0 },
	{ "frintx", VS_FMISC, 0, 0x2e219800, 0, 0 },
	{ "frintz", VS_FMISC, 0, 0x0ea19800, 0, 0 },
	{ "fsqrt", VS_FMISC, 0, 0x2ea1f800, 0, 0 },
	{ "fsub", VS_FSAME, 0, 0x0ea0d400, 0, 0 },
	{ "mla", VS_SAME, VF_NO2D, 0x0e209400, 0x2f000000, 0 },
	{ "mls", VS_SAME, VF_NO2D, 0x2e209400, 0x2f004000, 0 },
	{ "mul", VS_SAME, VF_NO2D, 0x0e209c00, 0x0f008000, 0 },
	{ "neg", VS_MISC, 0, 0x2e20b800, 0, 0 },
	{ "rshrn", VS_SHRN, 0, 0x0f008c00, 0, 0 },
	{ "rshrn2", VS_SHRN, 0, 0x4f008c00, 0, 0 },
	{ "sabal", VS_LONG, 0, 0x0e205000, 0, 0 },
	{ "sabal2", VS_LONG, 0, 0x4e205000, 0, 0 },
	{ "sabd", VS_SAME, VF_NO2D, 0x0e207400, 0, 0 },
	{ "sabdl", VS_LONG, 0, 0x0e207000, 0, 0 },
	{ "sabdl2", VS_LONG, 0, 0x4e207000, 0, 0 },
	{ "saddl", VS_LONG, 0, 0x0e200000, 0, 0 },
	{ "saddl2", VS_LONG, 0, 0x4e200000, 0, 0 },
	{ "saddw", VS_LONG, VF_WIDE, 0x0e201000, 0, 0 },
	{ "saddw2", VS_LONG, VF_WIDE, 0x4e201000, 0, 0 },
	{ "scvtf", VS_FMISC, 0, 0x0e21d800, 0, 0 },
	{ "shrn", VS_SHRN, 0, 0x0f008400, 0, 0 },
	{ "shrn2", VS_SHRN, 0, 0x4f008400, 0, 0 },
	{ "smax", VS_SAME, VF_NO2D, 0x0e206400, 0, 0 },
	{ "smaxp", VS_SAME, VF_NO2D, 0x0e20a400, 0, 0 },
	{ "smin", VS_SAME, VF_NO2D, 0x0e206c00, 0, 0 },
	{ "sminp", VS_SAME, VF_NO2D, 0x0e20ac00, 0, 0 },
	{ "smlal", VS_LONG, 0, 0x0e208000, 0x0f002000, 0 },
	{ "smlal2", VS_LONG, 0, 0x4e208000, 0x4f002000, 0 },
	{ "smlsl", VS_LONG, 0, 0x0e20a000, 0x0f006000, 0 },
	{ "smlsl2", VS_LONG, 0, 0x4e20a000, 0x4f006000, 0 },
	{ "smull", VS_LONG, 0, 0x0e20c000, 0x0f00a000, 0 },
	{ "smull2", VS_LONG, 0, 0x4e20c000, 0x4f00a000, 0 },
	{ "sqxtn", VS_XTN, 0, 0x0e214800, 0, 0 },
	{ "sqxtn2", VS_XTN, 0, 0x4e214800, 0, 0 },
	{ "sqxtun", VS_XTN, 0, 0x2e212800, 0, 0 },
	{ "sqxtun2", VS_XTN, 0, 0x6e212800, 0, 0 },
	{ "sshl", VS_SAME, 0, 0x0e204400, 0, 0 },
	{ "sshll", VS_SHLL, 0, 0x0f00a400, 0, 0 },
	{ "sshll2", VS_SHLL, 0, 0x4f00a400, 0, 0 },
	{ "ssubl", VS_LONG, 0, 0x0e202000, 0, 0 },
	{ "ssubl2", VS_LONG, 0, 0x4e202000, 0, 0 },
	{ "ssubw", VS_LONG, VF_WIDE, 0x0e203000, 0, 0 },
	{ "ssubw2", VS_LONG, VF_WIDE, 0x4e203000, 0, 0 },
	{ "sxtl", VS_SHLL, 0, 0x0f00a400, 0, 0 },
	{ "sxtl2", VS_SHLL, 0, 0x4f00a400, 0, 0 },
	{ "uabal", VS_LONG, 0, 0x2e205000, 0, 0 },
	{ "uabal2", VS_LONG, 0, 0x6e205000, 0, 0 },
	{ "uabd", VS_SAME, VF_NO2D, 0x2e207400, 0, 0 },
	{ "uabdl", VS_LONG, 0, 0x2e207000, 0, 0 },
	{ "uabdl2", VS_LONG, 0, 0x6e207000, 0, 0 },
	{ "uaddl", VS_LONG, 0, 0x2e200000, 0, 0 },
	{ "uaddl2", VS_LONG, 0, 0x6e200000, 0, 0 },
	{ "uaddw", VS_LONG, VF_WIDE, 0x2e201000, 0, 0 },
	{ "uaddw2", VS_LONG, VF_WIDE, 0x6e201000, 0, 0 },
	{ "ucvtf", VS_FMISC, 0, 0x2e21d800, 0, 0 },
	{ "umaxp", VS_SAME, VF_NO2D, 0x2e20a400, 0, 0 },
	{ "uminp", VS_SAME, VF_NO2D, 0x2e20ac00, 0, 0 },
	{ "umlal", VS_LONG, 0, 0x2e208000, 0x2f002000, 0 },
	{ "umlal2", VS_LONG, 0, 0x6e208000, 0x6f002000, 0 },
	{ "umlsl", VS_LONG, 0, 0x2e20a000, 0x2f006000, 0 },
	{ "umlsl2", VS_LONG, 0, 0x6e20a000, 0x6f006000, 0 },
	{ "umull", VS_LONG, 0, 0x2e20c000, 0x2f00a000, 0 },
	{ "umull2", VS_LONG, 0, 0x6e20c000, 0x6f00a000, 0 },
	{ "uqxtn", VS_XTN, 0, 0x2e214800, 0, 0 },
	{ "uqxtn2", VS_XTN, 0, 0x6e214800, 0, 0 },
	{ "ushl", VS_SAME, 0, 0x2e204400, 0, 0 },
	{ "ushll", VS_SHLL, 0, 0x2f00a400, 0, 0 },
	{ "ushll2", VS_SHLL, 0, 0x6f00a400, 0, 0 },
	{ "usubl", VS_LONG, 0, 0x2e202000, 0, 0 },
	{ "usubl2", VS_LONG, 0, 0x6e202000, 0, 0 },
	{ "usubw", VS_LONG, VF_WIDE, 0x2e203000, 0, 0 },
	{ "usubw2", VS_LONG, VF_WIDE, 0x6e203000, 0, 0 },
	{ "uxtl", VS_SHLL, 0, 0x2f00a400, 0, 0 },
	{ "uxtl2", VS_SHLL, 0, 0x6f00a400, 0, 0 },
	{ "xtn", VS_XTN, 0, 0x0e212800, 0, 0 },
	{ "xtn2", VS_XTN, 0, 0x4e212800, 0, 0 },
};

static const Vf *vlookup(const char *mn, size_t n)
{
	size_t lo = 0, hi = sizeof vtab / sizeof vtab[0];
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = strncmp(vtab[mid].name, mn, n);
		if (!r && vtab[mid].name[n]) r = 1;
		if (!r) return &vtab[mid];
		if (r < 0) lo = mid + 1; else hi = mid;
	}
	return 0;
}

static int isvec(const A64Op *o) { return o->kind == A64O_VREG && o->arr && o->arr < A64_1Q; }
static int iszero(const A64Op *o) { return (o->kind == A64O_FIMM && o->fimm == 0) || (isimm(o) && !o->hassym && !o->imm); }

/* the wide arrangement of a narrow one (8b -> 8h, 16b -> 8h, ...) */
static int arr_wide(unsigned arr)
{
	switch (arr) {
	case A64_8B: case A64_16B: return A64_8H;
	case A64_4H: case A64_8H: return A64_4S;
	case A64_2S: case A64_4S: return A64_2D;
	}
	return -1;
}

static int k_simd(Ctx *c, const Vf *v)
{
	const A64Op *o = c->op;
	int q2 = (v->base >> 30) & 1;	/* the "2" (upper half) forms */
	switch (v->sub) {
	case VS_SAME: {
		if (c->n != 3 || !isvec(&o[0]) || !isvec(&o[1]) || o[1].arr != o[0].arr) return 1;
		int sz = arr_size(o[0].arr);
		if (o[2].kind == A64O_VELEM && v->b2) {	/* by element: H (v0-v15) or S */
			unsigned i = o[2].idx, r = o[2].reg;
			if (o[2].arr != A64_ELB + sz) BADOPS;	/* the lane is Vn's element size */
			if (sz == 1) { if (i > 7 || r > 15) BADOPS; *c->out = v->b2 | (i >> 2) << 11 | (i & 3) << 20; }
			else if (sz == 2) { if (i > 3) BADOPS; *c->out = v->b2 | (i >> 1) << 11 | (i & 1) << 21; }
			else BADOPS;
			*c->out |= arr_q(o[0].arr) << 30 | (uint32_t)sz << 22 | r << 16 | R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		if (!isvec(&o[2]) || o[2].arr != o[0].arr || sz < 0 || ((v->fl & VF_NO2D) && sz == 3)) return 1;
		*c->out = v->base | arr_q(o[0].arr) << 30 | (uint32_t)sz << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case VS_FSAME: {
		if (c->n != 3 || !isvec(&o[0]) || !isvec(&o[1]) || o[1].arr != o[0].arr) return 1;
		unsigned a = o[0].arr;
		if ((a == A64_4H || a == A64_8H) && v->base && isvec(&o[2]) && o[2].arr == a) {	/* FP16: 0 Q U 01110 a 10 Rm 00 opc 1 Rn Rd */
			*c->out = ((v->base & ~0x0020c000u) | 0x00400000u) | arr_q(a) << 30 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		if (a != A64_2S && a != A64_4S && a != A64_2D) return 1;
		unsigned sz = a == A64_2D;
		if (o[2].kind == A64O_VELEM && v->b2) {
			unsigned i = o[2].idx;
			if (sz ? i > 1 : i > 3) BADOPS;
			*c->out = v->b2 | arr_q(a) << 30 | sz << 22 | (sz ? i << 11 : (i >> 1) << 11 | (i & 1) << 21) |
				  R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		if (iszero(&o[2]) && v->b3) { *c->out = v->b3 | arr_q(a) << 30 | sz << 22 | R(&o[1]) << 5 | R(&o[0]); return 0; }
		if (!isvec(&o[2]) || o[2].arr != a || !v->base) return 1;
		*c->out = v->base | arr_q(a) << 30 | sz << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case VS_MISC: case VS_CMZ: {
		if (c->n != (v->sub == VS_CMZ ? 3 : 2) || !isvec(&o[0]) || !isvec(&o[1]) || o[1].arr != o[0].arr) return 1;
		if (v->sub == VS_CMZ && !iszero(&o[2])) return 1;
		int sz = arr_size(o[0].arr);
		if (sz == 3 && !arr_q(o[0].arr)) BADOPS;
		*c->out = v->base | arr_q(o[0].arr) << 30 | (uint32_t)sz << 22 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case VS_FMISC: {
		if (c->n != 2 || !isvec(&o[0]) || !isvec(&o[1]) || o[1].arr != o[0].arr) return 1;
		unsigned a = o[0].arr;
		if (a == A64_4H || a == A64_8H) {	/* FP16: bits 22..17 = 111100 */
			*c->out = (v->base & ~(0x3fu << 17)) | 0x3cu << 17 | arr_q(a) << 30 | R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		if (a != A64_2S && a != A64_4S && a != A64_2D) BADOPS;
		*c->out = v->base | arr_q(a) << 30 | (uint32_t)(a == A64_2D) << 22 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case VS_LONG: {	/* Vd wide; Vn narrow (wide for the w forms), Vm narrow */
		if (c->n == 3 && isvec(&o[0]) && isvec(&o[1]) && o[2].kind == A64O_VELEM && v->b2) {
			/* by element (smull Vd.2d, Vn.2s, Vm.s[i]): H (v0-v15) or S lanes */
			unsigned na = o[1].arr, i = o[2].idx, r = o[2].reg;
			int sz = arr_size(na);
			if (arr_q(na) != (unsigned)q2 || arr_wide(na) != (int)o[0].arr || o[2].arr != A64_ELB + sz) BADOPS;
			if (sz == 1) { if (i > 7 || r > 15) BADOPS; *c->out = v->b2 | (i >> 2) << 11 | (i & 3) << 20; }
			else if (sz == 2) { if (i > 3) BADOPS; *c->out = v->b2 | (i >> 1) << 11 | (i & 1) << 21; }
			else BADOPS;
			*c->out |= (uint32_t)sz << 22 | r << 16 | R(&o[1]) << 5 | R(&o[0]);
			return 0;
		}
		if (c->n != 3 || !isvec(&o[0]) || !isvec(&o[1]) || !isvec(&o[2])) return 1;
		unsigned na = o[2].arr;
		if (arr_q(na) != (unsigned)q2 || arr_wide(na) != (int)o[0].arr) BADOPS;
		if ((v->fl & VF_WIDE) ? o[1].arr != o[0].arr : o[1].arr != na) BADOPS;
		*c->out = v->base | (uint32_t)arr_size(na) << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case VS_SHLL: {	/* sshll Vd.wide, Vn.narrow, #sh ; sxtl = #0 */
		int ext = v->name[1] == 'x';
		if (c->n != (ext ? 2 : 3) || !isvec(&o[0]) || !isvec(&o[1])) return 1;
		unsigned na = o[1].arr;
		if (arr_q(na) != (unsigned)q2 || arr_wide(na) != (int)o[0].arr) BADOPS;
		int64_t sh = 0;
		if (!ext) { if (!iscimm(&o[2])) BADOPS; sh = o[2].imm; }
		unsigned es = 8u << arr_size(na);
		if (sh < 0 || sh >= (int64_t)es) FAIL("immediate out of range");
		*c->out = v->base | (es + (unsigned)sh) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case VS_XTN: case VS_SHRN: {	/* Vd.narrow, Vn.wide [, #sh] */
		if (c->n != (v->sub == VS_SHRN ? 3 : 2) || !isvec(&o[0]) || !isvec(&o[1])) return 1;
		unsigned na = o[0].arr;
		if (arr_q(na) != (unsigned)q2 || arr_wide(na) != (int)o[1].arr) BADOPS;
		if (v->sub == VS_XTN) { *c->out = v->base | (uint32_t)arr_size(na) << 22 | R(&o[1]) << 5 | R(&o[0]); return 0; }
		if (!iscimm(&o[2])) BADOPS;
		unsigned es = 8u << arr_size(na);
		int64_t sh = o[2].imm;
		if (sh < 1 || sh > (int64_t)es) FAIL("immediate out of range");
		*c->out = v->base | (2 * es - (unsigned)sh) << 16 | R(&o[1]) << 5 | R(&o[0]);
		return 0;
	}
	case VS_FMOV: {	/* fmov Vd.2s|4s|2d, #imm */
		if (c->n != 2 || !isvec(&o[0]) || (o[1].kind != A64O_FIMM && !(isimm(&o[1]) && !o[1].hassym))) return 1;
		unsigned a = o[0].arr, imm8;
		if (a != A64_2S && a != A64_4S && a != A64_2D) BADOPS;
		double d = o[1].kind == A64O_FIMM ? o[1].fimm : (double)o[1].imm;
		if (fp_imm8(d, &imm8)) FAIL("expected compatible register or floating-point constant");
		*c->out = v->base | arr_q(a) << 30 | (uint32_t)(a == A64_2D) << 29 | (imm8 >> 5) << 16 | (imm8 & 31) << 5 | R(&o[0]);
		return 0;
	}
	}
	return 1;
}

/* ---------------------------------------------------------------- dispatch */

#include "sve.inc"

int a64_asm(const char *mn, size_t n, const A64Op *op, int nop, uint32_t *out, A64Fix *fix, const char **err)
{
	Ctx c = { op, nop, out, fix, err, mn };
	fix->type = 0;
	*err = 0;
	const Mn *m = lookup(mn, n);
	/* none of the table's names is a b.cond, an LSUI form or an atomic,
	 * except the exclusives, which k_atomic encodes */
	if (!m || m->k == K_LDX || m->k == K_STX) {
		/* b.cond / bcond */
		if (n >= 3 && (mn[0] | 32) == 'b') {
			int cc = -1;
			if (mn[1] == '.' && n == 4) cc = a64_cond(mn + 2, 2);
			else if (n == 3) cc = a64_cond(mn + 1, 2);
			if (cc >= 0) return bcond(&c, cc);
		}
		int r;
		if (k_atomic(&c, mn, n, &r)) return r;
		if (k_lsui(&c, mn, n, &r)) return r;
	}
	if (sve_insn(mn, n, op, nop)) { int r = k_sve(&c, mn, n); if (r != 1) return r; }
	if (nop == 2 && op[0].kind == A64O_VREG && op[1].kind == A64O_VREG && n >= 5 && mn[0] == 'f' && mn[1] == 'c' && mn[2] == 'v' && mn[3] == 't') {
		/* fcvtn{2} / fcvtxn{2} (narrow), fcvtl{2} (long) */
		int two = mn[n - 1] == '2', len = (int)n - two;
		int nar = (len == 5 && mn[4] == 'n') || (len == 6 && mn[4] == 'x' && mn[5] == 'n'), lng = len == 5 && mn[4] == 'l';
		if (nar || lng) {
			unsigned w = nar ? op[0].arr : op[1].arr, d = nar ? op[1].arr : op[0].arr;	/* narrow side, wide side */
			int sz = d == A64_2D ? 1 : d == A64_4S ? 0 : -1;
			unsigned want = sz ? (two ? A64_4S : A64_2S) : (two ? A64_8H : A64_4H);
			if (sz < 0 || w != want || (len == 6 && !sz)) { *err = "invalid operand for instruction"; return -1; }
			uint32_t b = len == 6 ? 0x2e216800u : nar ? 0x0e216800u : 0x0e217800u;
			*out = b | (uint32_t)two << 30 | (uint32_t)sz << 22 | (uint32_t)op[1].reg << 5 | op[0].reg;
			return 0;
		}
	}
	if (nop == 2 && op[0].kind == A64O_REG && op[1].kind == A64O_VREG && n >= 5 && mn[0] == 'f' && mn[n - 1] == 'p') {
		/* faddp/fmaxp/fminp/fmaxnmp/fminnmp Vd, Vn.2T (scalar pairwise) */
		static const struct { const char *n; uint32_t b; } fp[] = {
			{ "faddp", 0x7e30d800 }, { "fmaxp", 0x7e30f800 }, { "fminp", 0x7eb0f800 },
			{ "fmaxnmp", 0x7e30c800 }, { "fminnmp", 0x7eb0c800 } };
		for (size_t i = 0; i < sizeof fp / sizeof fp[0]; i++) if (strlen(fp[i].n) == n && !memcmp(fp[i].n, mn, n)) {
			unsigned cl = op[0].cls, a = op[1].arr;
			uint32_t b = fp[i].b;
			if (cl == A64_S && a == A64_2S) ;
			else if (cl == A64_D && a == A64_2D) b |= 1u << 22;
			else { *err = "invalid operand for instruction"; return -1; }
			*out = b | (uint32_t)op[1].reg << 5 | op[0].reg;
			return 0;
		}
	}
	if (nop == 3 && op[0].kind == A64O_REG && op[2].kind == A64O_VELEM && n >= 4 && mn[0] == 'f' &&
	    (op[0].cls == A64_S || op[0].cls == A64_D) && op[1].kind == A64O_REG && op[1].cls == op[0].cls) {
		/* fmul/fmla/fmls/fmulx Sd|Dd, Sn|Dn, Vm.T[i] */
		uint32_t b = n == 4 && !memcmp(mn, "fmul", 4) ? 0x5f809000u : n == 4 && !memcmp(mn, "fmla", 4) ? 0x5f801000u :
			     n == 4 && !memcmp(mn, "fmls", 4) ? 0x5f805000u : n == 5 && !memcmp(mn, "fmulx", 5) ? 0x7f809000u : 0;
		if (b) {
			int d = op[0].cls == A64_D;
			unsigned i = op[2].idx;
			if (op[2].arr != (d ? A64_ELD : A64_ELS) || i > (d ? 1u : 3u)) { *err = "invalid operand for instruction"; return -1; }
			*out = b | (uint32_t)d << 22 | (d ? i << 11 : (i >> 1) << 11 | (i & 1) << 21) |
			       (uint32_t)op[2].reg << 16 | (uint32_t)op[1].reg << 5 | op[0].reg;
			return 0;
		}
	}
	if (nop == 3 && op[0].kind == A64O_VREG && n >= 4 && !memcmp(mn + n - 3, "dot", 3) &&
	    (n == 4 ? mn[0] == 's' || mn[0] == 'u' : n == 5 && !memcmp(mn, "usdot", 5))) {
		/* sdot/udot/usdot Vd.2s|4s, Vn.8b|16b, Vm.8b|16b */
		int q = op[0].arr == A64_4S;
		if (!(q || op[0].arr == A64_2S) || op[1].kind != A64O_VREG || op[2].kind != A64O_VREG ||
		    op[1].arr != (q ? A64_16B : A64_8B) || op[2].arr != op[1].arr) { *err = "invalid operand for instruction"; return -1; }
		uint32_t b = n == 5 ? 0x0e809c00u : mn[0] == 'u' ? 0x2e809400u : 0x0e809400u;
		*out = b | (uint32_t)q << 30 | (uint32_t)op[2].reg << 16 | (uint32_t)op[1].reg << 5 | op[0].reg;
		return 0;
	}
	if (nop && op[0].kind == A64O_VREG) {
		const Vf *v = vlookup(mn, n);
		if (v) { int r = k_simd(&c, v); if (r != 1) return r; if (!m) { *err = "invalid operand for instruction"; return -1; } }
	}
	if (!m) { *err = "unrecognized instruction mnemonic"; return -1; }
	switch (m->k) {
	case K_ADDSUB: return k_addsub(&c, m);
	case K_CMP: return k_cmp(&c, m);
	case K_NEG: return k_neg(&c, m);
	case K_LOGIC: return k_logic(&c, m);
	case K_TST: return k_tst(&c, m);
	case K_MOV: return k_mov(&c, m);
	case K_MOVW: return k_movw(&c, m);
	case K_SHIFT: return k_shift(&c, m);
	case K_BFX: return k_bfx(&c, m);
	case K_BFM: return k_bfm(&c, m);
	case K_BFI: return k_bfi(&c, m);
	case K_EXTR: return k_extr(&c, m);
	case K_ADR: return k_adr(&c, m);
	case K_B: return k_b(&c, m);
	case K_CBZ: return k_cbz(&c, m);
	case K_TBZ: return k_tbz(&c, m);
	case K_BR: return k_br(&c, m);
	case K_RET: return k_ret(&c, m);
	case K_LDST: return k_ldst(&c, m);
	case K_LDSTU: return k_ldstu(&c, m);
	case K_LDSTT: return k_ldstt(&c, m);
	case K_LDP: return k_ldp(&c, m);
	case K_LDX: return k_ldx(&c, m);
	case K_STX: return k_stx(&c, m);
	case K_PRFM: return k_prfm(&c, m);
	case K_FIXED: return k_fixed(&c, m);
	case K_HINT: return k_hint(&c, m);
	case K_BTI: return k_bti(&c, m);
	case K_EXC: return k_exc(&c, m);
	case K_BARRIER: return k_barrier(&c, m);
	case K_MSR: return k_msr(&c, m);
	case K_MRS: return k_mrs(&c, m);
	case K_DC: return k_dc(&c, m);
	case K_IC: return k_ic(&c, m);
	case K_TLBI: return k_tlbi(&c, m);
	case K_SYS: return k_sys(&c, m);
	case K_CSEL: return k_csel(&c, m);
	case K_CSET: return k_cset(&c, m);
	case K_CNEG: return k_cneg(&c, m);
	case K_CCMP: return k_ccmp(&c, m);
	case K_DP1: return k_dp1(&c, m);
	case K_DP2: return k_dp2(&c, m);
	case K_MUL: return k_mul(&c, m);
	case K_IALIAS: return k_ialias(&c, m);
	case K_PAC: return k_pac(&c, m);
	case K_CRC: return k_crc(&c, m);
	case K_VLD1: return k_vld1(&c, m);
	case K_VREV: return k_vrev(&c, m);
	case K_DUP: return k_dup(&c, m);
	case K_EXT: return k_ext(&c, m);
	case K_MOVI: return k_movi(&c, m);
	case K_VSHIFT: return k_vshift(&c, m);
	case K_VPERM: return k_vperm(&c, m);
	case K_PMULL: return k_pmull(&c, m);
	case K_FMOV: return k_fmov(&c, m);
	case K_FP: return k_fp(&c, m);
	case K_SHA3: return k_sha3(&c, m);
	case K_SHA2: return k_sha2(&c, m);
	case K_CPY: return k_cpy(&c, m);
	case K_SET: return k_set(&c, m);
	case K_UMOV: return k_umov(&c, m);
	case K_AT: return k_at(&c, m);
	case K_RDVL: return k_rdvl(&c, m);
	case K_PREG: return k_preg(&c, m);
	case K_V2: return k_v2(&c, m);
	case K_V3: return k_v3(&c, m);
	case K_TBL: return k_tbl(&c, m);
	case K_SHA512: return k_sha512(&c, m);
	case K_LDR_REP: return k_ldr_rep(&c, m);
	case K_INS: return k_ins(&c, m);
	case K_MTE: return k_mte(&c, m);
	case K_MTEM: return k_mtem(&c, m);
	case K_VNOT: return k_vnot(&c, m);
	case K_SMOV: return k_smov(&c, m);
	case K_ACROSS: return k_across(&c, m);
	case K_VMISC: return k_vmisc(&c, m);
	case K_ADDP: return k_addp(&c, m);
	}
	*err = "internal: unhandled class";
	return -1;
}
