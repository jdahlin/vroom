/* vcc code generator: object backend: records -> machine code */
#include "../../../codegen/emitimpl.h"

static uint32_t r_label(rctx *R, uint32_t fid, uint32_t l)
{
	if (R->lser[l] == R->ser)
		return R->lsym[l];
	R->nb.n = 0;
	p_lbl(&R->nb, fid, l);
	R->lser[l] = R->ser;
	return R->lsym[l] = as_symbol_by_name(R->as, R->nb.p, R->nb.n);
}

static unsigned e_movi(uint32_t *w, int rd, int x, uint64_t v)
{
	int nz = 0, nf = 0, chunks = x ? 4 : 2, n = 0;
	bool neg, first = true;
	uint64_t mask = x ? ~0ull : 0xffffffffull;

	v &= mask;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		nz += h != 0;
		nf += h == 0xffff;
	}
	if (nz <= 1 || (chunks - nf) <= 1 || logimm_ok(v, x)) {
		/* mov: movz, movn, or orr with a bitmask immediate (as.c mov_imm) */
		unsigned hw;

		if (rd != SP) {
			for (hw = 0; hw < (unsigned)chunks; hw++)
				if (!(v & ~(0xffffull << 16 * hw))) {
					w[0] = a64_movz(x, (unsigned)rd, (unsigned)(v >> 16 * hw) & 0xffff, hw);
					return 1;
				}
			for (hw = 0; hw < (unsigned)chunks; hw++)
				if (!(~v & mask & ~(0xffffull << 16 * hw))) {
					w[0] = a64_movn(x, (unsigned)rd, (unsigned)(~v >> 16 * hw) & 0xffff, hw);
					return 1;
				}
		}
		w[0] = a64_logic_imm(x, 1, (unsigned)rd & 31, 31, (unsigned)a64_logimm(v, x));
		return 1;
	}
	neg = nf > nz;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		if (h == (neg ? 0xffff : 0))
			continue;
		if (first) {
			w[n++] = neg ? a64_movn(x, (unsigned)rd, (uint16_t)~h, (unsigned)i) : a64_movz(x, (unsigned)rd, h, (unsigned)i);
			first = false;
		} else {
			w[n++] = a64_movk(x, (unsigned)rd, h, (unsigned)i);
		}
	}
	return (unsigned)n;
}

static inline unsigned R5(int r) { return r == SP ? 31u : (unsigned)r & 31; }	/* SP and ZR are both 31 */

/* add/sub (register), the extended form when sp is involved (as.c addsub_reg) */
static uint32_t e_addsub_reg(int sf, int op, int s, int rd, int rn, int rm, unsigned kind, unsigned amt)
{
	if (kind >= 8)	/* extended register: option kind - 8 */
		return 0x0b200000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | R5(rm) << 16 |
		       (kind - 8) << 13 | (amt & 7) << 10 | R5(rn) << 5 | R5(rd);
	if (rd == SP || rn == SP)
		return 0x0b200000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | R5(rm) << 16 |
		       (sf ? 3u : 2u) << 13 | (amt & 7) << 10 | R5(rn) << 5 | R5(rd);
	return a64_addsub_reg(sf, op, s, R5(rd), R5(rn), R5(rm), kind, amt);
}

static const uint8_t ld_opc[4][3] = { { 1, 2, 3 }, { 1, 2, 3 }, { 1, 2, 1 }, { 1, 1, 1 } };

/* encode one record into w (returns the word count; the fixup, if any, in *f) */
static unsigned enc(const ins *i, uint32_t *w, pfix *f, const uint32_t *loff, uint32_t at)
{
	int sf = i->sz;

	f->type = 0;
	switch (i->op) {
	case I_LABEL: case I_RAW: case I_NOP: case I_MOVSP:
		return 0;
	case I_ADD: case I_SUB: case I_ADDS: case I_SUBS:
		w[0] = e_addsub_reg(sf, i->op == I_SUB || i->op == I_SUBS, i->op == I_ADDS || i->op == I_SUBS,
				    i->rd, i->rn, i->rm, i->sh || i->cond >= 8 ? i->cond : 0, i->sh);
		return 1;
	case I_AND: case I_ORR: case I_EOR: case I_ANDS: case I_BIC: case I_ORN: {
		static const uint8_t opc[] = { [I_AND] = 0, [I_ORR] = 1, [I_EOR] = 2, [I_ANDS] = 3, [I_BIC] = 0, [I_ORN] = 1 };

		w[0] = a64_logic_reg(sf, opc[i->op], i->op == I_BIC || i->op == I_ORN, R5(i->rd), R5(i->rn), R5(i->rm),
				     i->sh ? i->cond : 0, i->sh);
		return 1;
	}
	case I_ADC: case I_SBC: case I_ADCS: case I_SBCS: {
		static const uint32_t b[] = { [I_ADC] = 0x1a000000, [I_SBC] = 0x5a000000, [I_ADCS] = 0x3a000000, [I_SBCS] = 0x7a000000 };

		w[0] = b[i->op] | (uint32_t)sf << 31 | R5(i->rm) << 16 | R5(i->rn) << 5 | R5(i->rd);
		return 1;
	}
	case I_ADDI: case I_SUBI: case I_ADDSI: case I_SUBSI: {
		int64_t v = i->imm;
		bool flags = i->op == I_ADDSI || i->op == I_SUBSI;
		bool sub = i->op == I_SUBI || i->op == I_SUBSI;
		unsigned n;

		if (v < 0 && v != INT64_MIN) {
			v = -v;
			sub = !sub;
		}
		if (!sf)
			v &= 0xffffffff;
		if (v >= 0 && (v < 4096 || ((v & 0xfff) == 0 && v < (1 << 24)) || (!flags && v < (1 << 24)))) {
			int rn = i->rn;

			n = 0;
			if (v >= 4096 && (v & 0xfff)) {
				w[n++] = a64_addsub_imm(sf, sub, flags, R5(i->rd), R5(rn), (unsigned)(v >> 12), 1);
				rn = i->rd;
				v &= 0xfff;
			}
			if ((v & 0xfff) == 0 && v)
				w[n++] = a64_addsub_imm(sf, sub, flags, R5(i->rd), R5(rn), (unsigned)(v >> 12), 1);
			else
				w[n++] = a64_addsub_imm(sf, sub, flags, R5(i->rd), R5(rn), (unsigned)v, 0);
			return n;
		}
		n = e_movi(w, R_X17, sf, (uint64_t)v);
		w[n] = e_addsub_reg(sf, sub, flags, i->rd, i->rn, R_X17, 0, 0);
		return n + 1;
	}
	case I_ANDI: case I_ORRI: case I_EORI: case I_ANDSI: {
		static const uint8_t opc[] = { [I_ANDI] = 0, [I_ORRI] = 1, [I_EORI] = 2, [I_ANDSI] = 3 };
		uint64_t v = sf ? (uint64_t)i->imm : (uint32_t)i->imm;

		w[0] = a64_logic_imm(sf, opc[i->op], R5(i->rd), R5(i->rn), (unsigned)a64_logimm(v, sf));
		return 1;
	}
	case I_MOVI:
		return e_movi(w, i->rd, sf, (uint64_t)i->imm);
	case I_MOV:
		if (i->rd == SP || i->rn == SP)
			w[0] = a64_addsub_imm(sf, 0, 0, R5(i->rd), R5(i->rn), 0, 0);
		else
			w[0] = a64_logic_reg(sf, 1, 0, R5(i->rd), 31, R5(i->rn), 0, 0);
		return 1;
	case I_MADD: case I_MSUB:
		w[0] = a64_madd(sf, i->op == I_MSUB, R5(i->rd), R5(i->rn), R5(i->rm), R5(i->ra));
		return 1;
	case I_SDIV: case I_UDIV: case I_LSLV: case I_LSRV: case I_ASRV: {
		static const uint8_t opc[] = { [I_SDIV] = 3, [I_UDIV] = 2, [I_LSLV] = 8, [I_LSRV] = 9, [I_ASRV] = 10 };

		w[0] = a64_dp2(sf, opc[i->op], R5(i->rd), R5(i->rn), R5(i->rm));
		return 1;
	}
	case I_SMULH: case I_UMULH:
		w[0] = (i->op == I_SMULH ? 0x9b407c00u : 0x9bc07c00u) | R5(i->rm) << 16 | R5(i->rn) << 5 | R5(i->rd);
		return 1;
	case I_SBFM: case I_UBFM: case I_BFM:
		w[0] = a64_bfm(sf, i->op == I_SBFM ? 0 : i->op == I_BFM ? 1 : 2, R5(i->rd), R5(i->rn), (unsigned)i->imm, i->sh);
		return 1;
	case I_CSEL: case I_CSINC: case I_CSINV: case I_CSNEG:
		w[0] = a64_csel(sf, i->op == I_CSINV || i->op == I_CSNEG, i->op == I_CSINC || i->op == I_CSNEG,
				R5(i->rd), R5(i->rn), R5(i->rm), i->cond);
		return 1;
	case I_CLZ: case I_CLS: case I_RBIT: case I_REV: case I_REV16: case I_REV32: {
		uint32_t b;

		switch (i->op) {
		case I_CLZ: b = 0x5ac01000u | (uint32_t)sf << 31; break;
		case I_CLS: b = 0x5ac01400u | (uint32_t)sf << 31; break;
		case I_RBIT: b = 0x5ac00000u | (uint32_t)sf << 31; break;
		case I_REV: b = 0x5ac00800u | (uint32_t)sf << 31 | (sf ? 0x400u : 0); break;
		case I_REV16: b = 0x5ac00400u | (uint32_t)sf << 31; break;
		default: b = 0xdac00800u; break;
		}
		w[0] = b | R5(i->rn) << 5 | R5(i->rd);
		return 1;
	}
	case I_LD: case I_ST: case I_LDF: case I_STF: {
		int64_t off = i->imm;
		int sz = i->sz, base = i->rn, v = i->op == I_LDF || i->op == I_STF;
		unsigned n = 0, opc = i->op == I_ST || i->op == I_STF ? 0 : v ? 1 : ld_opc[sz][i->cond];
		bool scaled = off >= 0 && (off & ((1 << sz) - 1)) == 0 && (off >> sz) < 4096;

		if (!scaled && !(off >= -256 && off < 256)) {
			n = e_movi(w, R_X17, 1, (uint64_t)off);
			w[n++] = e_addsub_reg(1, 0, 0, R_X17, base, R_X17, 0, 0);
			base = R_X17;
			off = 0;
			scaled = true;
		}
		w[n++] = scaled ? a64_ldst_uoff((unsigned)sz, v, opc, R5(i->rd), R5(base), (unsigned)(off >> sz))
				: a64_ldst_imm9((unsigned)sz, v, opc, 0, R5(i->rd), R5(base), (int)off);
		return n;
	}
	case I_SMADDL: case I_UMADDL:
		w[0] = (i->op == I_SMADDL ? 0x9b200000u : 0x9ba00000u) | R5(i->rm) << 16 | R5(i->ra) << 10 | R5(i->rn) << 5 |
		       R5(i->rd);
		return 1;
	case I_LDX: case I_STX:	/* register offset */
		w[0] = (uint32_t)i->sz << 30 | 0x38200800u | (i->op == I_STX ? 0 : ld_opc[i->sz][i->cond]) << 22 |
		       R5(i->rm) << 16 | (uint32_t)i->ra << 13 | (i->sh ? 1u << 12 : 0) | R5(i->rn) << 5 | R5(i->rd);
		return 1;
	case I_LDP: case I_STP:
		w[0] = a64_ldstp(2, 0, i->cond == 0 ? 2 : i->cond == 1 ? 3 : 1, i->op == I_LDP, R5(i->rd), R5(i->ra), R5(i->rn),
				 (int)(i->imm / 8));
		return 1;
	case I_ADRP:
		w[0] = 0x90000000u | R5(i->rd);
		*f = (pfix){ 0, 0, R_AARCH64_ADR_PREL_PG_HI21, i->ref, i->off };
		return 1;
	case I_ADDLO:
		w[0] = a64_addsub_imm(1, 0, 0, R5(i->rd), R5(i->rn), 0, 0);
		*f = (pfix){ 0, 0, R_AARCH64_ADD_ABS_LO12_NC, i->ref, i->off };
		return 1;
	case I_LDLO: case I_STLO: {
		static const uint32_t lo12[] = { R_AARCH64_LDST8_ABS_LO12_NC, R_AARCH64_LDST16_ABS_LO12_NC,
						 R_AARCH64_LDST32_ABS_LO12_NC, R_AARCH64_LDST64_ABS_LO12_NC };
		unsigned opc = i->op == I_STLO ? 0 : ld_opc[i->sz][i->cond];

		w[0] = a64_ldst_uoff(i->sz, 0, opc, R5(i->rd), R5(i->rn), 0);
		*f = (pfix){ 0, 0, lo12[i->sz], i->ref, i->off };
		return 1;
	}
	case I_LDGOT:
		/* two fixups: the caller handles I_LDGOT's second word */
		w[0] = 0x90000000u | R5(i->rd);
		w[1] = a64_ldst_uoff(3, 0, 1, R5(i->rd), R5(i->rd), 0);
		*f = (pfix){ 0, 0, R_AARCH64_ADR_GOT_PAGE, i->ref, 0 };
		return 2;
	case I_ADR:
		if (loff) {
			w[0] = a64_adr(R5(i->rd), ((int64_t)loff[i->label] - at) * 4);
			return 1;
		}
		w[0] = 0x10000000u | R5(i->rd);
		*f = (pfix){ 0, 1, R_AARCH64_ADR_PREL_LO21, i->label, 0 };
		return 1;
	case I_JTE:
		if (loff) {
			w[0] = (uint32_t)(((int64_t)loff[i->label] - loff[i->ref]) * 4);
			return 1;
		}
		w[0] = 0;
		*f = (pfix){ 0, 1, R_AARCH64_ADR_PREL_LO21, i->label, 0 };
		return 1;
	case I_B: case I_BC: case I_CBZ: case I_CBNZ: case I_TBZ: case I_TBNZ: {
		int64_t d = loff ? ((int64_t)loff[i->label] - at) * 4 : 0;

		switch (i->op) {
		case I_B: w[0] = a64_b(d); break;
		case I_BC: w[0] = a64_bcond(i->cond, d); break;
		case I_TBZ: case I_TBNZ: w[0] = a64_tbz(i->op == I_TBNZ, R5(i->rd), i->sh, d); break;
		default: w[0] = a64_cbz(sf, i->op == I_CBNZ, R5(i->rd), d); break;
		}
		if (!loff)
			*f = (pfix){ 0, 1, i->op == I_B ? R_AARCH64_JUMP26 : i->op == I_TBZ || i->op == I_TBNZ ? R_AARCH64_TSTBR14 :
					   R_AARCH64_CONDBR19, i->label, 0 };
		return 1;
	}
	case I_TAIL:
		if (!i->cond)
			return 0;
		w[0] = 0x14000000u;
		*f = (pfix){ 0, 0, R_AARCH64_JUMP26, i->ref, 0 };
		return 1;
	case I_BL:
		w[0] = 0x94000000u;
		*f = (pfix){ 0, 0, R_AARCH64_CALL26, i->ref, 0 };
		return 1;
	case I_BLR:
		w[0] = a64_blr(R5(i->rn));
		return 1;
	case I_BR:
		w[0] = a64_br(R5(i->rn));
		return 1;
	case I_RET:
		w[0] = a64_ret(30);
		return 1;
	case I_BRK:
		w[0] = 0xd4200000u | ((uint32_t)i->imm & 0xffff) << 5;
		return 1;
	case I_PRFM:
		w[0] = 0xf9800000u | R5(i->rn) << 5;	/* prfm pldl1keep */
		return 1;
	case I_MRS:
		w[0] = 0;	/* the caller looks the register up */
		return 1;
	case I_WORD:
		w[0] = (uint32_t)i->imm;
		return 1;
	case I_TLV:
		switch (i->cond) {
		case TLS_LE:	/* mrs x1, tpidr_el0; add x0, x1, :tprel_hi12:; add x0, x0, :tprel_lo12_nc: */
			w[0] = 0xd53bd041u;
			w[1] = 0x91400020u;
			w[2] = 0x91000000u;
			*f = (pfix){ 1, 0, R_AARCH64_TLSLE_ADD_TPREL_HI12, i->ref, 0 };
			return 3;
		case TLS_IE:	/* mrs x1, tpidr_el0; adrp x0, :gottprel:; ldr x0, [x0, :gottprel_lo12:]; add x0, x1, x0 */
			w[0] = 0xd53bd041u;
			w[1] = 0x90000000u;
			w[2] = a64_ldst_uoff(3, 0, 1, 0, 0, 0);
			w[3] = 0x8b000020u;
			*f = (pfix){ 1, 0, R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21, i->ref, 0 };
			return 4;
		case TLS_DESC:	/* adrp x0; ldr x1, [x0]; add x0, x0; blr x1 (.tlsdesccall); mrs x1, tpidr_el0; add x0, x1, x0 */
			w[0] = 0x90000000u;
			w[1] = a64_ldst_uoff(3, 0, 1, 1, 0, 0);
			w[2] = 0x91000000u;
			w[3] = a64_blr(1);
			w[4] = 0xd53bd041u;
			w[5] = 0x8b000020u;
			*f = (pfix){ 0, 0, R_AARCH64_TLSDESC_ADR_PAGE21, i->ref, 0 };
			return 6;
		}
		/* adrp x0, ref@TLVPPAGE; ldr x0, [x0, ref@TLVPPAGEOFF]; ldr x16, [x0]; blr x16 */
		w[0] = 0x90000000u;
		w[1] = a64_ldst_uoff(3, 0, 1, 0, 0, 0);
		w[2] = a64_ldst_uoff(3, 0, 1, 16, 0, 0);
		w[3] = a64_blr(16);
		*f = (pfix){ 0, 0, R_VCC_TLVP_PAGE21, i->ref, 0 };
		return 4;
	}
	return 0;
}

/* an upper bound on the words record i takes (inline asm: unknown, a lot) */
VPRIV uint32_t a64_words(const ins *i)
{
	uint32_t w[16];
	pfix f;

	return i->op == I_RAW ? 1u << 20 : enc(i, w, &f, NULL, 0);
}

/* the fixups after an instruction's first: off is where the instruction starts */
static void more_fixups(ElfObj *o, uint32_t frag, uint64_t off, uint8_t *p, const ins *i, uint32_t y, as_context *as)
{
	if (i->op == I_LDGOT)
		elf_fixup(o, frag, off + 4, p + 4, R_AARCH64_LD64_GOT_LO12_NC, as_symbol_value(as, y, 0));
	else if (i->op != I_TLV)
		return;
	else if (i->cond == 0)
		elf_fixup(o, frag, off + 4, p + 4, R_VCC_TLVP_PAGEOFF12, as_symbol_value(as, y, 0));
	else if (i->cond == TLS_LE)
		elf_fixup(o, frag, off + 8, p + 8, R_AARCH64_TLSLE_ADD_TPREL_LO12_NC, as_symbol_value(as, y, 0));
	else if (i->cond == TLS_IE)
		elf_fixup(o, frag, off + 8, p + 8, R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC, as_symbol_value(as, y, 0));
	else {
		elf_fixup(o, frag, off + 4, p + 4, R_AARCH64_TLSDESC_LD64_LO12, as_symbol_value(as, y, 0));
		elf_fixup(o, frag, off + 8, p + 8, R_AARCH64_TLSDESC_ADD_LO12, as_symbol_value(as, y, 0));
		elf_fixup(o, frag, off + 12, p + 12, R_AARCH64_TLSDESC_CALL, as_symbol_value(as, y, 0));
	}
}

/* emit one instruction's words at the end of frag, with its fixup(s) */
static void r_words(rctx *R, uint32_t frag, const uint32_t *w, unsigned n, const pfix *f, uint32_t fid, const ins *i)
{
	ElfObj *o = R->o;
	uint64_t off;
	uint8_t *p;

	elf_code(o, frag);
	off = elf_frag(o, frag)->size;
	p = elf_grow(o, frag, (size_t)n * 4);
	for (unsigned k = 0; k < n; k++)
		memcpy(p + 4 * k, &w[k], 4);
	if (f->type) {
		uint32_t y = f->lbl ? r_label(R, fid, f->ref) : r_sym(R, f->ref);

		elf_fixup(o, frag, off + 4u * f->k, p + 4 * f->k, f->type, as_symbol_value(R->as, y, f->off));
		more_fixups(o, frag, off, p, i, y, R->as);
	}
}

static inline int hexval(char ch)
{
	return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;	/* vtext_hex_digits_append: lower case */
}

/* per label of a function with inline asm (valid if ser == rctx.ser) */
enum { RL_NAMED = 1, RL_PLACED = 2, RL_DEFSYM = 4 };

static inline rlab *r_lab(rctx *R, uint32_t l)
{
	rlab *L = &R->lab[l];

	if (L->ser != R->ser)
		*L = (rlab){ R->ser, 0, 0, 0, 0 };
	return L;
}

/* label-relative ranges, in words: b.cond/cbz/cbnz imm19, adr 21 bits of bytes, b imm26 */
#define BR19_MAX ((1 << 18) - 1)
#define BR19_MIN (-(1 << 18))

/* the branch record i at (frag, off, p) to placed label L */
static bool r_branch(rctx *R, uint32_t fid, const ins *i, rlab *L, uint32_t frag, uint32_t off, uint8_t *p)
{
	uint32_t w[4];
	pfix f;

	if (L->frag == frag && !(L->off & 3) && !(off & 3)) {
		int64_t d = ((int64_t)L->off - off) / 4;

		if ((i->op == I_TBZ || i->op == I_TBNZ) && (d < -(1 << 13) || d >= 1 << 13))
			return r_fail(R, "function with inline asm too large: tbz beyond 32 KB") == 0;
		if (i->op != I_B && (d < BR19_MIN || d > BR19_MAX))	/* not relaxed with inline asm in the function */
			return r_fail(R, "function with inline asm too large: conditional branch or adr beyond 1 MB") == 0;
		R->loff[i->label] = L->off / 4;
		enc(i, w, &f, R->loff, off / 4);
		memcpy(p, w, 4);
		return true;
	}
	{
		uint32_t y = r_label(R, fid, i->label);

		if (!(L->flags & RL_DEFSYM)) {
			if (elf_symp(R->o, y)->flags & ESYM_DEFINED)
				return r_fail(R, "invalid symbol redefinition") == 0;
			elf_define(R->o, y, L->frag, L->off);
			L->flags |= RL_DEFSYM;
		}
		enc(i, w, &f, NULL, 0);
		memcpy(p, w, 4);
		elf_fixup(R->o, frag, off, p, f.type, as_symbol_value(R->as, y, 0));
	}
	return true;
}

static inline bool is_cbr(unsigned op) { return op == I_BC || op == I_CBZ || op == I_CBNZ; }

/* first rpatch with at >= pos (pt is sorted by at) */
static uint32_t pt_lb(const rpatch *pt, uint32_t npt, uint32_t pos)
{
	uint32_t lo = 0, hi = npt;

	while (lo < hi) {
		uint32_t m = lo + (hi - lo) / 2;

		if (pt[m].at < pos)
			lo = m + 1;
		else
			hi = m;
	}
	return lo;
}

/*
 * Branch relaxation for a function encoded in one block (r_code, no inline
 * asm): a conditional branch whose label is beyond +-1 MB becomes the
 * inverted branch over an unconditional one (b.!c .+8; b label), as LLVM's
 * BranchRelaxation does.  Only functions over 1 MB get here.  Relaxing one
 * branch moves the others, so this iterates until nothing more grows
 * (branches only ever get longer, so it terminates).  Then the words in wb
 * are spread out, and labels (loff) and records (pt) move with them.
 * Returns the number of words added.
 */
static uint32_t r_relax(const ins *in, uint32_t nin, uint32_t *wb, uint32_t at, uint32_t *loff, rpatch *pt, uint32_t npt)
{
	static _Thread_local uint32_t *cum;	/* cum[j]: relaxed branches among pt[0..j) */
	uint32_t nl = 0, s;
	bool grew = true;

	if (!cum)
		cum = reserve((size_t)1 << 32);
	while (grew) {
		grew = false;
		cum[0] = 0;
		for (uint32_t j = 0; j < npt; j++)
			cum[j + 1] = cum[j] + pt[j].lng;
		for (uint32_t j = 0; j < npt; j++) {
			const ins *i = &in[pt[j].k];
			uint32_t t;
			int64_t d;

			if (pt[j].lng || !pt[j].f.lbl || !is_cbr(i->op))
				continue;
			t = loff[i->label];
			d = (int64_t)t + cum[pt_lb(pt, npt, t)] - ((int64_t)pt[j].at + cum[j]);
			if (d < BR19_MIN || d > BR19_MAX) {
				pt[j].lng = 1;
				grew = true;
			}
		}
	}
	nl = cum[npt];
	if (!nl)
		return 0;
	/* spread the words: everything after a relaxed branch moves by the relaxed ones up to it */
	for (uint32_t j = npt, end_ = at; j-- > 0; ) {
		uint32_t b = pt[j].at;

		if (!pt[j].lng)
			continue;
		s = cum[j + 1];
		memmove(wb + b + 1 + s, wb + b + 1, (size_t)(end_ - b - 1) * 4);
		wb[b + s - 1] = wb[b];
		end_ = b;
	}
	for (uint32_t k = 0; k < nin; k++)
		if (in[k].op == I_LABEL)
			loff[in[k].label] += cum[pt_lb(pt, npt, loff[in[k].label])];
	for (uint32_t j = 0; j < npt; j++)
		pt[j].at += cum[j];
	return nl;
}

VPRIV int a64_replay(rctx *R, const code_hdr *h)
{
	const ins *in = (const ins *)(h + 1);
	const char *raw = (const char *)(in + h->nin);
	uint32_t w[16];
	pfix f;

	if (!h->sym_labels) {
		/*
		 * One block, branches resolved here: encode everything once into
		 * R->wb, noting the records that need a label offset (patched
		 * once the labels are known) or a fixup (applied in order after
		 * the words are in the frag).
		 */
		uint32_t at = 0, frag = as_fragment_current(R->as), *loff = R->loff, *wb = R->wb, npt = 0;
		rpatch *pt = R->pt;
		ElfObj *o = R->o;
		uint64_t base;
		uint8_t *p;

		for (uint32_t k = 0; k < h->nin; k++) {
			const ins *i = &in[k];
			unsigned n;

			if (i->op == I_LABEL) {
				loff[i->label] = at;
				continue;
			}
			n = enc(i, wb + at, &pt[npt].f, NULL, 0);
			if (unlikely(i->op == I_MRS)) {
				int r = a64_sysreg(R->c->strs[i->ref].s, strlen(R->c->strs[i->ref].s), 0);

				if (r < 0)
					return r_fail(R, "mrs: expected readable system register");
				wb[at] = 0xd5200000u | ((unsigned)r & 0xffff) << 5 | R5(i->rd);
			}
			if (pt[npt].f.type) {
				pt[npt].k = k;
				pt[npt].at = at;
				pt[npt].lng = 0;
				npt++;
			}
			at += n;
		}
		if (unlikely(at > BR19_MAX))	/* a label can be out of a conditional branch's reach */
			at += r_relax(in, h->nin, wb, at, loff, pt, npt);
		elf_code(o, frag);
		base = elf_frag(o, frag)->size;
		p = elf_grow(o, frag, (size_t)at * 4);
		for (uint32_t j = 0; j < npt; j++) {
			const ins *i = &in[pt[j].k];
			uint32_t a = pt[j].at;
			int64_t d;

			if (!pt[j].f.lbl)
				continue;
			if (pt[j].lng) {
				/* b.!cond / cbnz<->cbz over the next word (the record was encoded with offset 0) */
				if (i->op == I_BC && i->cond >= 14)
					return r_fail(R, "internal: relaxing an unconditional b.al");
				wb[a] = (i->op == I_BC ? wb[a] ^ 1 : wb[a] ^ 1u << 24) | 2u << 5;
				wb[a + 1] = a64_b(((int64_t)loff[i->label] - (a + 1)) * 4);
				continue;
			}
			d = (int64_t)loff[i->label] - a;
			if (i->op == I_TBZ || i->op == I_TBNZ ? d < -(1 << 13) || d >= 1 << 13 :
			    is_cbr(i->op) ? d < BR19_MIN || d > BR19_MAX :
			    i->op == I_ADR ? d < BR19_MIN || d > BR19_MAX :
			    i->op == I_B ? d < -(1 << 25) || d >= 1 << 25 : false)
				return r_fail(R, i->op == I_ADR ? "function too large: adr to a label beyond 1 MB" :
							   "function too large: branch out of range");
			enc(i, wb + a, &f, loff, a);
		}
		memcpy(p, wb, (size_t)at * 4);
		for (uint32_t j = 0; j < npt; j++) {
			const pfix *g = &pt[j].f;
			uint32_t a = pt[j].at, y;

			if (g->lbl)
				continue;
			y = r_sym(R, g->ref);
			elf_fixup(o, frag, base + 4u * (a + g->k), p + 4 * (a + g->k), g->type, as_symbol_value(R->as, y, g->off));
			more_fixups(o, frag, base + 4u * a, p + 4 * a, &in[pt[j].k], y, R->as);
		}
		return 0;
	}
	/*
	 * Inline asm: in order.  Labels the asm text names (asm goto) are
	 * symbols; branches to the others are resolved here when label and
	 * branch end up in the same frag, else through the label's symbol.
	 */
	{
		ElfObj *o = R->o;
		uint32_t npend = 0;

		R->ser++;
		for (const char *q = raw, *qe = raw + h->nraw; (q = memmem(q, (size_t)(qe - q), ".LBB", 4)); ) {
			uint32_t fid = 0, l = 0;
			const char *d = q += 4;

			for (int v; q < qe && (v = hexval(*q)) >= 0; q++)
				fid = fid * 16 + (uint32_t)v;
			if (q == d || q >= qe || *q != '_' || fid != h->fid)
				continue;
			d = ++q;
			for (int v; q < qe && (v = hexval(*q)) >= 0; q++)
				l = l * 16 + (uint32_t)v;
			if (q > d && l < h->nlabel) {
				rlab *L = r_lab(R, l);

				L->flags |= RL_NAMED;
			}
		}
		for (uint32_t k = 0; k < h->nin; k++) {
			const ins *i = &in[k];
			unsigned n;

			if (i->op == I_RAW) {
				if (as_snippet_assemble(R->as, raw + i->off, i->ref))
					return -1;
				continue;
			}
			if (i->op == I_LABEL) {
				rlab *L = r_lab(R, i->label);
				uint32_t frag = as_fragment_current(R->as);

				if (L->flags & RL_NAMED) {
					if (as_label_define_symbol(R->as, r_label(R, h->fid, i->label)))
						return -1;
					continue;
				}
				L->flags |= RL_PLACED;
				L->frag = frag;
				L->off = (uint32_t)elf_frag(o, frag)->size;
				for (uint32_t j = L->pend; j; j = R->pend[j - 1].next) {
					const rpend *pd = &R->pend[j - 1];

					if (!r_branch(R, h->fid, &in[pd->k], L, pd->frag, pd->off, pd->p))
						return -1;
				}
				continue;
			}
			if (i->op == I_ADR || i->op == I_B || i->op == I_BC || i->op == I_CBZ || i->op == I_CBNZ || i->op == I_TBZ ||
			    i->op == I_TBNZ) {
				rlab *L = r_lab(R, i->label);

				if (!(L->flags & RL_NAMED)) {
					uint32_t frag = as_fragment_current(R->as), off;
					uint8_t *p;

					elf_code(o, frag);
					off = (uint32_t)elf_frag(o, frag)->size;
					p = elf_grow(o, frag, 4);
					if (L->flags & RL_PLACED) {
						if (!r_branch(R, h->fid, i, L, frag, off, p))
							return -1;
					} else {
						rpend *pd = &R->pend[npend++];

						*pd = (rpend){ L->pend, k, frag, off, p };
						L->pend = npend;
						enc(i, w, &f, NULL, 0);
						memcpy(p, w, 4);
					}
					continue;
				}
			}
			n = enc(i, w, &f, NULL, 0);
			if (unlikely(i->op == I_MRS)) {
				int r = a64_sysreg(R->c->strs[i->ref].s, strlen(R->c->strs[i->ref].s), 0);

				if (r < 0)
					return r_fail(R, "mrs: expected readable system register");
				w[0] = 0xd5200000u | ((unsigned)r & 0xffff) << 5 | R5(i->rd);
			}
			if (n)
				r_words(R, as_fragment_current(R->as), w, n, &f, h->fid, i);
		}
		/* branches to labels never placed: through the (undefined) symbol, as the text would */
		for (uint32_t j = 0; j < npend; j++) {
			const rpend *pd = &R->pend[j];
			const ins *i = &in[pd->k];

			if (!(r_lab(R, i->label)->flags & RL_PLACED)) {
				enc(i, w, &f, NULL, 0);
				elf_fixup(o, pd->frag, pd->off, pd->p, f.type, as_symbol_value(R->as, r_label(R, h->fid, i->label), 0));
			}
		}
	}
	return 0;
}
