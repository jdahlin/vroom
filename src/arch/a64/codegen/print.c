/* vcc code generator: printer: records -> assembly text */
#include "../../../codegen/emitimpl.h"

/* ---------------- printer: records -> assembly text ---------------- */

VPRIV void p_lbl(tbuf *b, uint32_t fid, uint32_t l)
{
	vtext_string_append(b, ".LBB");
	vtext_hex_digits_append(b, fid);
	vtext_char_append(b, '_');
	vtext_hex_digits_append(b, l);
}

static void p_movi(tbuf *b, int rd, int x, uint64_t v)
{
	int nz = 0, nf = 0, chunks = x ? 4 : 2;
	bool first = true, neg;

	if (!x)
		v &= 0xffffffffu;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		nz += h != 0;
		nf += h == 0xffff;
	}
	if (nz <= 1 || (chunks - nf) <= 1 || logimm_ok(v, x)) {
		vtext_string_append(b, "\tmov\t");
		reg(b, rd, x);
		vtext_string_append(b, ", #");
		if (x)
			vtext_hex_prefixed_append(b, v);
		else
			vtext_hex_prefixed_append(b, (uint32_t)v);
		vtext_char_append(b, '\n');
		return;
	}
	neg = nf > nz;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		if (h == (neg ? 0xffff : 0))
			continue;
		if (first) {
			vtext_string_append(b, neg ? "\tmovn\t" : "\tmovz\t");
			h = neg ? (uint16_t)~h : h;
			first = false;
		} else {
			vtext_string_append(b, "\tmovk\t");
		}
		reg(b, rd, x);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, h);
		if (i) {
			vtext_string_append(b, ", lsl #");
			vtext_unsigned_append(b, (uint64_t)(16 * i));
		}
		vtext_char_append(b, '\n');
	}
	if (first)
		p_movi(b, rd, x, 0);
}

static const char ld_name[2][4][3][8] = {
	{ { "ldrb", "ldrsb", "ldrsb" }, { "ldrh", "ldrsh", "ldrsh" }, { "ldr", "ldrsw", "ldr" }, { "ldr", "ldr", "ldr" } },
	{ { "ldurb", "ldursb", "ldursb" }, { "ldurh", "ldursh", "ldursh" }, { "ldur", "ldursw", "ldur" }, { "ldur", "ldur", "ldur" } },
};
static const char st_name[2][4][8] = { { "strb", "strh", "str", "str" }, { "sturb", "sturh", "stur", "stur" } };

static inline int ld_x(const ins *i) { return i->sz == 3 || ((i->op == I_LD || i->op == I_LDX || i->op == I_LDLO) && i->cond == 1); }

static void p_mem(tbuf *b, const ins *i)
{
	int64_t off = i->imm;
	int sz = i->sz, scaled, base = i->rn;
	int x = i->op == I_ST ? sz == 3 : ld_x(i), v = i->op == I_LDF || i->op == I_STF;

	scaled = off >= 0 && (off & ((1 << sz) - 1)) == 0 && (off >> sz) < 4096;
	if (!scaled && !(off >= -256 && off < 256)) {
		p_movi(b, R_X17, 1, (uint64_t)off);
		vtext_string_append(b, "\tadd\tx17, ");
		reg(b, base, 1);
		vtext_string_append(b, ", x17\n");
		base = R_X17;
		off = 0;
		scaled = 1;
	}
	vtext_char_append(b, '\t');
	if (i->op == I_ST || i->op == I_STF)
		vtext_name_append(b, st_name[!scaled][sz]);
	else
		vtext_name_append(b, ld_name[!scaled][sz][v ? 0 : i->cond]);
	vtext_char_append(b, '\t');
	if (v) {
		vtext_char_append(b, sz == 3 ? 'd' : 's');
		vtext_unsigned_append(b, i->rd);
	} else {
		reg(b, i->rd, x);
	}
	vtext_string_append(b, ", [");
	reg(b, base, 1);
	if (off) {
		vtext_string_append(b, ", #");
		vtext_signed_append(b, off);
	}
	vtext_string_append(b, "]\n");
}

static const char alu_name[][8] = {
	[I_ADD] = "add", [I_SUB] = "sub", [I_ADDS] = "adds", [I_SUBS] = "subs", [I_AND] = "and",
	[I_ORR] = "orr", [I_EOR] = "eor", [I_ANDS] = "ands", [I_BIC] = "bic", [I_ORN] = "orn",
	[I_ADC] = "adc", [I_SBC] = "sbc", [I_ADCS] = "adcs", [I_SBCS] = "sbcs",
	[I_ADDI] = "add", [I_SUBI] = "sub", [I_ADDSI] = "adds", [I_SUBSI] = "subs",
	[I_ANDI] = "and", [I_ORRI] = "orr", [I_EORI] = "eor", [I_ANDSI] = "ands",
	[I_MADD] = "madd", [I_MSUB] = "msub", [I_SDIV] = "sdiv", [I_UDIV] = "udiv",
	[I_SMULH] = "smulh", [I_UMULH] = "umulh", [I_LSLV] = "lsl", [I_LSRV] = "lsr", [I_ASRV] = "asr",
	[I_SBFM] = "sbfm", [I_UBFM] = "ubfm", [I_BFM] = "bfm",
	[I_CSEL] = "csel", [I_CSINC] = "csinc", [I_CSINV] = "csinv", [I_CSNEG] = "csneg",
	[I_CLZ] = "clz", [I_CLS] = "cls", [I_RBIT] = "rbit", [I_REV] = "rev", [I_REV16] = "rev16", [I_REV32] = "rev32",
};

static void p_rrr(tbuf *b, const char *mn, const ins *i)
{
	vtext_char_append(b, '\t');
	vtext_name_append(b, mn);
	vtext_char_append(b, '\t');
	reg(b, i->rd, i->sz);
	vtext_string_append(b, ", ");
	reg(b, i->rn, i->sz);
	vtext_string_append(b, ", ");
	reg(b, i->rm, i->sz);
}

static void p_addi(tbuf *b, const ins *i)
{
	int op = i->op;
	int64_t v = i->imm;
	bool flags = op == I_ADDSI || op == I_SUBSI;
	bool sub = op == I_SUBI || op == I_SUBSI;

	if (v < 0 && v != INT64_MIN) {
		v = -v;
		sub = !sub;
	}
	if (!i->sz)
		v &= 0xffffffff;
	if (v >= 0 && (v < 4096 || ((v & 0xfff) == 0 && v < (1 << 24)) || (!flags && v < (1 << 24)))) {
		const char *mn = sub ? (flags ? "subs" : "sub") : (flags ? "adds" : "add");
		int rn = i->rn;

		if (v >= 4096 && (v & 0xfff)) {
			vtext_char_append(b, '\t'), vtext_string_append(b, mn), vtext_char_append(b, '\t');
			reg(b, i->rd, i->sz);
			vtext_string_append(b, ", ");
			reg(b, rn, i->sz);
			vtext_string_append(b, ", #"), vtext_signed_append(b, v >> 12), vtext_string_append(b, ", lsl #12\n");
			rn = i->rd;
			v &= 0xfff;
		}
		vtext_char_append(b, '\t'), vtext_string_append(b, mn), vtext_char_append(b, '\t');
		reg(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		reg(b, rn, i->sz);
		if ((v & 0xfff) == 0 && v)
			vtext_string_append(b, ", #"), vtext_signed_append(b, v >> 12), vtext_string_append(b, ", lsl #12\n");
		else
			vtext_string_append(b, ", #"), vtext_signed_append(b, v), vtext_char_append(b, '\n');
		return;
	}
	p_movi(b, R_X17, i->sz, (uint64_t)v);
	vtext_char_append(b, '\t'), vtext_string_append(b, sub ? (flags ? "subs" : "sub") : (flags ? "adds" : "add")), vtext_char_append(b, '\t');
	reg(b, i->rd, i->sz);
	vtext_string_append(b, ", ");
	reg(b, i->rn, i->sz);
	vtext_string_append(b, i->sz ? ", x17\n" : ", w17\n");
}

/* a register name (x0, w30, b1, q7, v31, sp, xzr, ...) */
static bool reg_like(const char *p, size_t n)
{
	static const char *const sp[] = { "sp", "wsp", "xzr", "wzr", "lr", "fp" };

	for (size_t k = 0; k < sizeof sp / sizeof *sp; k++)
		if (n == strlen(sp[k]) && !memcmp(p, sp[k], n))
			return true;
	if (n < 2 || n > 3 || !strchr("bhsdqvwxz", p[0]) || p[1] < '0' || p[1] > '9')
		return false;
	unsigned r = p[1] - '0';

	if (n == 3) {
		if (p[2] < '0' || p[2] > '9' || p[1] == '0')
			return false;
		r = r * 10 + (p[2] - '0');
	}
	return r < 32;
}

VPRIV void print_ins(cc *c, tbuf *b, const ins *i, uint32_t fid, const char *raw)
{
	switch (i->op) {
	case I_LABEL:
		p_lbl(b, fid, i->label);
		vtext_string_append(b, ":\n");
		return;
	case I_RAW:
		if (i->cond)	/* inline asm between clang's markers (bench/vcc-fpcheck.py) */
			vtext_string_append(b, "\t//APP\n");
		vtext_append(b, raw + i->off, i->ref);
		vtext_char_append(b, '\n');
		if (i->cond)
			vtext_string_append(b, "\t//NO_APP\n");
		return;
	case I_ADD: case I_SUB: case I_ADDS: case I_SUBS: case I_AND: case I_ORR: case I_EOR:
	case I_ANDS: case I_BIC: case I_ORN: case I_ADC: case I_SBC: case I_ADCS: case I_SBCS:
		if (i->cond >= 8) {	/* extended register */
			static const char xn[8][5] = { "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx" };
			int o = i->cond - 8;

			vtext_char_append(b, '\t'), vtext_name_append(b, alu_name[i->op]), vtext_char_append(b, '\t');
			reg(b, i->rd, i->sz), vtext_string_append(b, ", ");
			reg(b, i->rn, i->sz), vtext_string_append(b, ", ");
			reg(b, i->rm, (o & 3) == 3), vtext_string_append(b, ", "), vtext_string_append(b, xn[o]);
			if (i->sh)
				vtext_string_append(b, " #"), vtext_unsigned_append(b, i->sh);
			vtext_char_append(b, '\n');
			return;
		}
		p_rrr(b, alu_name[i->op], i);
		if (i->sh) {
			vtext_string_append(b, i->cond == 0 ? ", lsl #" : i->cond == 1 ? ", lsr #" : ", asr #");
			vtext_unsigned_append(b, i->sh);
		}
		vtext_char_append(b, '\n');
		return;
	case I_ADDI: case I_SUBI: case I_ADDSI: case I_SUBSI:
		p_addi(b, i);
		return;
	case I_ANDI: case I_ORRI: case I_EORI: case I_ANDSI:
		vtext_char_append(b, '\t');
		vtext_name_append(b, alu_name[i->op]);
		vtext_char_append(b, '\t');
		reg(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		reg(b, i->rn, i->sz);
		vtext_string_append(b, ", #");
		vtext_hex_prefixed_append(b, i->sz ? (uint64_t)i->imm : (uint32_t)i->imm);
		vtext_char_append(b, '\n');
		return;
	case I_MOVI:
		p_movi(b, i->rd, i->sz, (uint64_t)i->imm);
		return;
	case I_MOV:
		vtext_string_append(b, "\tmov\t");
		reg(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		reg(b, i->rn, i->sz);
		vtext_char_append(b, '\n');
		return;
	case I_MADD: case I_MSUB:
		p_rrr(b, alu_name[i->op], i);
		vtext_string_append(b, ", ");
		reg(b, i->ra, i->sz);
		vtext_char_append(b, '\n');
		return;
	case I_SDIV: case I_UDIV: case I_SMULH: case I_UMULH: case I_LSLV: case I_LSRV: case I_ASRV:
		p_rrr(b, alu_name[i->op], i);
		vtext_char_append(b, '\n');
		return;
	case I_SBFM: case I_UBFM: case I_BFM:
		vtext_char_append(b, '\t');
		vtext_name_append(b, alu_name[i->op]);
		vtext_char_append(b, '\t');
		reg(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		reg(b, i->rn, i->sz);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, (uint64_t)i->imm);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, i->sh);
		vtext_char_append(b, '\n');
		return;
	case I_CSEL: case I_CSINC: case I_CSINV: case I_CSNEG:
		p_rrr(b, alu_name[i->op], i);
		vtext_string_append(b, ", ");
		vtext_name_append(b, cond_name[i->cond]);
		vtext_char_append(b, '\n');
		return;
	case I_CLZ: case I_CLS: case I_RBIT: case I_REV: case I_REV16: case I_REV32:
		vtext_char_append(b, '\t');
		vtext_name_append(b, alu_name[i->op]);
		vtext_char_append(b, '\t');
		reg(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		reg(b, i->rn, i->sz);
		vtext_char_append(b, '\n');
		return;
	case I_LD: case I_ST: case I_LDF: case I_STF:
		p_mem(b, i);
		return;
	case I_SMADDL: case I_UMADDL:
		vtext_string_append(b, i->op == I_SMADDL ? "\tsmaddl\t" : "\tumaddl\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", ");
		reg(b, i->rn, 0);
		vtext_string_append(b, ", ");
		reg(b, i->rm, 0);
		vtext_string_append(b, ", ");
		reg(b, i->ra, 1);
		vtext_char_append(b, '\n');
		return;
	case I_LDX: case I_STX:
		vtext_char_append(b, '\t');
		vtext_name_append(b, i->op == I_STX ? st_name[0][i->sz] : ld_name[0][i->sz][i->cond]);
		vtext_char_append(b, '\t');
		reg(b, i->rd, i->op == I_STX ? i->sz == 3 : ld_x(i));
		vtext_string_append(b, ", [");
		reg(b, i->rn, 1);
		vtext_string_append(b, ", ");
		reg(b, i->rm, i->ra == 3);
		if (i->ra != 3)
			vtext_string_append(b, i->ra == 6 ? ", sxtw" : ", uxtw");
		else if (i->sh)
			vtext_string_append(b, ", lsl");
		if (i->sh) {
			vtext_string_append(b, " #");
			vtext_signed_append(b, i->sz);
		}
		vtext_string_append(b, "]\n");
		return;
	case I_LDP: case I_STP:
		vtext_string_append(b, i->op == I_LDP ? "\tldp\t" : "\tstp\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", ");
		reg(b, i->ra, 1);
		vtext_string_append(b, ", [");
		reg(b, i->rn, 1);
		if (i->cond == 2) {
			vtext_string_append(b, "], #");
			vtext_signed_append(b, i->imm);
			vtext_char_append(b, '\n');
		} else {
			if (i->imm) {
				vtext_string_append(b, ", #");
				vtext_signed_append(b, i->imm);
			}
			vtext_string_append(b, i->cond == 1 ? "]!\n" : "]\n");
		}
		return;
	case I_ADRP:
		vtext_string_append(b, "\tadrp\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", ");
		ref_off(c, b, i->ref, i->off);
		vtext_char_append(b, '\n');
		return;
	case I_ADDLO:
		vtext_string_append(b, "\tadd\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", ");
		reg(b, i->rn, 1);
		vtext_string_append(b, ", :lo12:");
		ref_off(c, b, i->ref, i->off);
		vtext_char_append(b, '\n');
		return;
	case I_LDLO: case I_STLO:
		vtext_char_append(b, '\t');
		vtext_string_append(b, i->op == I_STLO ? st_name[0][i->sz] : ld_name[0][i->sz][i->cond]);
		vtext_char_append(b, '\t');
		reg(b, i->rd, i->op == I_STLO ? i->sz == 3 : ld_x(i));
		vtext_string_append(b, ", [");
		reg(b, i->rn, 1);
		vtext_string_append(b, ", :lo12:");
		ref_off(c, b, i->ref, i->off);
		vtext_string_append(b, "]\n");
		return;
	case I_LDGOT:
		vtext_string_append(b, "\tadrp\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", :got:");
		ref_name(c, b, i->ref);
		vtext_string_append(b, "\n\tldr\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", [");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", :got_lo12:");
		ref_name(c, b, i->ref);
		vtext_string_append(b, "]\n");
		return;
	case I_JTE:
		vtext_string_append(b, "\t.word\t");
		p_lbl(b, fid, i->label);
		vtext_char_append(b, '-');
		p_lbl(b, fid, i->ref);
		vtext_char_append(b, '\n');
		return;
	case I_ADR:
		vtext_string_append(b, "\tadr\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", ");
		p_lbl(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case I_B:
		vtext_string_append(b, "\tb\t");
		p_lbl(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case I_BC:
		vtext_string_append(b, "\tb.");
		vtext_name_append(b, cond_name[i->cond]);
		vtext_char_append(b, '\t');
		p_lbl(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case I_CBZ: case I_CBNZ:
		vtext_string_append(b, i->op == I_CBZ ? "\tcbz\t" : "\tcbnz\t");
		reg(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		p_lbl(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case I_TBZ: case I_TBNZ:
		vtext_string_append(b, i->op == I_TBZ ? "\ttbz\t" : "\ttbnz\t");
		reg(b, i->rd, i->sz);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, i->sh);
		vtext_string_append(b, ", ");
		p_lbl(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case I_TAIL:
		if (!i->cond)
			return;
		/* symbol quoting is identical for bl and b. */
		__attribute__((fallthrough));
	case I_BL: {
		vtext_string_append(b, i->op == I_TAIL ? "\tb\t" : "\tbl\t");
		size_t at = b->n;

		ref_name(c, b, i->ref);
		if (reg_like(b->p + at, b->n - at)) {
			/* "bl b1" is a register operand to the assembler (llvm-mc too): quote it */
			size_t n = b->n - at;

			vtext_char_append(b, '"');
			vtext_char_append(b, '"');
			memmove(b->p + at + 1, b->p + at, n);
			b->p[at] = '"';
		}
		vtext_char_append(b, '\n');
		return;
	}
	case I_BLR: case I_BR:
		vtext_string_append(b, i->op == I_BLR ? "\tblr\t" : "\tbr\t");
		reg(b, i->rn, 1);
		vtext_char_append(b, '\n');
		return;
	case I_RET:
		vtext_string_append(b, "\tret\n");
		return;
	case I_BRK:
		vtext_string_append(b, "\tbrk\t#");
		vtext_hex_prefixed_append(b, (uint64_t)i->imm);
		vtext_char_append(b, '\n');
		return;
	case I_PRFM:
		vtext_string_append(b, "\tprfm\tpldl1keep, [");
		reg(b, i->rn, 1);
		vtext_string_append(b, "]\n");
		return;
	case I_MRS:
		vtext_string_append(b, "\tmrs\t");
		reg(b, i->rd, 1);
		vtext_string_append(b, ", ");
		vtext_string_append(b, c->strs[i->ref].s);
		vtext_char_append(b, '\n');
		return;
	case I_NOP:
		return;
	case I_WORD:
		vtext_string_append(b, "\t.inst\t");
		vtext_hex_prefixed_append(b, (uint32_t)i->imm);
		vtext_char_append(b, '\n');
		return;
	case I_TLV: {
		/* ELF: the sequences of obj.c; x0 = the address */
		static const char *const seq[4][7] = {
			{ "\tadrp\tx0, :vcc_tlvp:", "\n\tldr\tx0, [x0, :vcc_tlvp_lo12:", "]\n\tldr\tx16, [x0]\n\tblr\tx16\n" },
			{ "\tmrs\tx1, TPIDR_EL0\n\tadd\tx0, x1, :tprel_hi12:", "\n\tadd\tx0, x0, :tprel_lo12_nc:", "\n" },
			{ "\tmrs\tx1, TPIDR_EL0\n\tadrp\tx0, :gottprel:", "\n\tldr\tx0, [x0, :gottprel_lo12:", "]\n\tadd\tx0, x1, x0\n" },
			{ "\tadrp\tx0, :tlsdesc:", "\n\tldr\tx1, [x0, :tlsdesc_lo12:", "]\n\tadd\tx0, x0, :tlsdesc_lo12:",
			  "\n\t.tlsdesccall\t", "\n\tblr\tx1\n\tmrs\tx1, TPIDR_EL0\n\tadd\tx0, x1, x0\n" },
		};
		const char *const *q = seq[i->cond < 4 ? i->cond : 0];

		for (int k = 0; q[k + 1]; k++) {
			vtext_string_append(b, q[k]);
			ref_name(c, b, i->ref);
		}
		for (int k = 0; q[k]; k++)
			if (!q[k + 1])
				vtext_string_append(b, q[k]);
		return;
	}
	default:
		vtext_string_append(b, "\t// ?\n");
		return;
	}
}
