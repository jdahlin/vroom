/* vcc code generator: printer: records -> assembly text */
#include "../../../emit/emitimpl.h"

/* ---------------- printer: records -> assembly text ---------------- */

VPRIV void p_lbl(tbuf *b, uint32_t fid, uint32_t l)
{
	tb_s(b, ".LBB");
	tb_x(b, fid);
	tb_c(b, '_');
	tb_x(b, l);
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
		tb_s(b, "\tmov\t");
		reg(b, rd, x);
		tb_s(b, ", #");
		if (x)
			tb_hex(b, v);
		else
			tb_hex(b, (uint32_t)v);
		tb_c(b, '\n');
		return;
	}
	neg = nf > nz;
	for (int i = 0; i < chunks; i++) {
		uint16_t h = (uint16_t)(v >> (16 * i));

		if (h == (neg ? 0xffff : 0))
			continue;
		if (first) {
			tb_s(b, neg ? "\tmovn\t" : "\tmovz\t");
			h = neg ? (uint16_t)~h : h;
			first = false;
		} else {
			tb_s(b, "\tmovk\t");
		}
		reg(b, rd, x);
		tb_s(b, ", #");
		tb_u(b, h);
		if (i) {
			tb_s(b, ", lsl #");
			tb_u(b, (uint64_t)(16 * i));
		}
		tb_c(b, '\n');
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
	int x = i->op == I_ST ? sz == 3 : ld_x(i);

	scaled = off >= 0 && (off & ((1 << sz) - 1)) == 0 && (off >> sz) < 4096;
	if (!scaled && !(off >= -256 && off < 256)) {
		p_movi(b, R_X17, 1, (uint64_t)off);
		tb_s(b, "\tadd\tx17, ");
		reg(b, base, 1);
		tb_s(b, ", x17\n");
		base = R_X17;
		off = 0;
		scaled = 1;
	}
	tb_c(b, '\t');
	if (i->op == I_ST)
		tb_n8(b, st_name[!scaled][sz]);
	else
		tb_n8(b, ld_name[!scaled][sz][i->cond]);
	tb_c(b, '\t');
	reg(b, i->rd, x);
	tb_s(b, ", [");
	reg(b, base, 1);
	if (off) {
		tb_s(b, ", #");
		tb_i(b, off);
	}
	tb_s(b, "]\n");
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
	tb_c(b, '\t');
	tb_n8(b, mn);
	tb_c(b, '\t');
	reg(b, i->rd, i->sz);
	tb_s(b, ", ");
	reg(b, i->rn, i->sz);
	tb_s(b, ", ");
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
			tb_c(b, '\t'), tb_s(b, mn), tb_c(b, '\t');
			reg(b, i->rd, i->sz);
			tb_s(b, ", ");
			reg(b, rn, i->sz);
			tb_s(b, ", #"), tb_i(b, v >> 12), tb_s(b, ", lsl #12\n");
			rn = i->rd;
			v &= 0xfff;
		}
		tb_c(b, '\t'), tb_s(b, mn), tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, rn, i->sz);
		if ((v & 0xfff) == 0 && v)
			tb_s(b, ", #"), tb_i(b, v >> 12), tb_s(b, ", lsl #12\n");
		else
			tb_s(b, ", #"), tb_i(b, v), tb_c(b, '\n');
		return;
	}
	p_movi(b, R_X17, i->sz, (uint64_t)v);
	tb_c(b, '\t'), tb_s(b, sub ? (flags ? "subs" : "sub") : (flags ? "adds" : "add")), tb_c(b, '\t');
	reg(b, i->rd, i->sz);
	tb_s(b, ", ");
	reg(b, i->rn, i->sz);
	tb_s(b, i->sz ? ", x17\n" : ", w17\n");
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
		tb_s(b, ":\n");
		return;
	case I_RAW:
		if (i->cond)	/* inline asm between clang's markers (bench/vcc-fpcheck.py) */
			tb_s(b, "\t//APP\n");
		tb_put(b, raw + i->off, i->ref);
		tb_c(b, '\n');
		if (i->cond)
			tb_s(b, "\t//NO_APP\n");
		return;
	case I_ADD: case I_SUB: case I_ADDS: case I_SUBS: case I_AND: case I_ORR: case I_EOR:
	case I_ANDS: case I_BIC: case I_ORN: case I_ADC: case I_SBC: case I_ADCS: case I_SBCS:
		p_rrr(b, alu_name[i->op], i);
		if (i->sh) {
			tb_s(b, i->cond == 0 ? ", lsl #" : i->cond == 1 ? ", lsr #" : ", asr #");
			tb_u(b, i->sh);
		}
		tb_c(b, '\n');
		return;
	case I_ADDI: case I_SUBI: case I_ADDSI: case I_SUBSI:
		p_addi(b, i);
		return;
	case I_ANDI: case I_ORRI: case I_EORI: case I_ANDSI:
		tb_c(b, '\t');
		tb_n8(b, alu_name[i->op]);
		tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_s(b, ", #");
		tb_hex(b, i->sz ? (uint64_t)i->imm : (uint32_t)i->imm);
		tb_c(b, '\n');
		return;
	case I_MOVI:
		p_movi(b, i->rd, i->sz, (uint64_t)i->imm);
		return;
	case I_MOV:
		tb_s(b, "\tmov\t");
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_c(b, '\n');
		return;
	case I_MADD: case I_MSUB:
		p_rrr(b, alu_name[i->op], i);
		tb_s(b, ", ");
		reg(b, i->ra, i->sz);
		tb_c(b, '\n');
		return;
	case I_SDIV: case I_UDIV: case I_SMULH: case I_UMULH: case I_LSLV: case I_LSRV: case I_ASRV:
		p_rrr(b, alu_name[i->op], i);
		tb_c(b, '\n');
		return;
	case I_SBFM: case I_UBFM: case I_BFM:
		tb_c(b, '\t');
		tb_n8(b, alu_name[i->op]);
		tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_s(b, ", #");
		tb_u(b, (uint64_t)i->imm);
		tb_s(b, ", #");
		tb_u(b, i->sh);
		tb_c(b, '\n');
		return;
	case I_CSEL: case I_CSINC: case I_CSINV: case I_CSNEG:
		p_rrr(b, alu_name[i->op], i);
		tb_s(b, ", ");
		tb_n8(b, cond_name[i->cond]);
		tb_c(b, '\n');
		return;
	case I_CLZ: case I_CLS: case I_RBIT: case I_REV: case I_REV16: case I_REV32:
		tb_c(b, '\t');
		tb_n8(b, alu_name[i->op]);
		tb_c(b, '\t');
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		reg(b, i->rn, i->sz);
		tb_c(b, '\n');
		return;
	case I_LD: case I_ST:
		p_mem(b, i);
		return;
	case I_SMADDL: case I_UMADDL:
		tb_s(b, i->op == I_SMADDL ? "\tsmaddl\t" : "\tumaddl\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		reg(b, i->rn, 0);
		tb_s(b, ", ");
		reg(b, i->rm, 0);
		tb_s(b, ", ");
		reg(b, i->ra, 1);
		tb_c(b, '\n');
		return;
	case I_ADDXT: case I_SUBXT:
		tb_s(b, i->op == I_ADDXT ? "\tadd\t" : "\tsub\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		reg(b, i->rn, 1);
		tb_s(b, ", ");
		reg(b, i->rm, 0);
		tb_s(b, i->ra == 6 ? ", sxtw" : ", uxtw");
		if (i->sh) {
			tb_s(b, " #");
			tb_u(b, i->sh);
		}
		tb_c(b, '\n');
		return;
	case I_LDX: case I_STX:
		tb_c(b, '\t');
		tb_n8(b, i->op == I_STX ? st_name[0][i->sz] : ld_name[0][i->sz][i->cond]);
		tb_c(b, '\t');
		reg(b, i->rd, i->op == I_STX ? i->sz == 3 : ld_x(i));
		tb_s(b, ", [");
		reg(b, i->rn, 1);
		tb_s(b, ", ");
		reg(b, i->rm, i->ra == 3);
		if (i->ra != 3)
			tb_s(b, i->ra == 6 ? ", sxtw" : ", uxtw");
		else if (i->sh)
			tb_s(b, ", lsl");
		if (i->sh) {
			tb_s(b, " #");
			tb_i(b, i->sz);
		}
		tb_s(b, "]\n");
		return;
	case I_LDP: case I_STP:
		tb_s(b, i->op == I_LDP ? "\tldp\t" : "\tstp\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		reg(b, i->ra, 1);
		tb_s(b, ", [");
		reg(b, i->rn, 1);
		if (i->cond == 2) {
			tb_s(b, "], #");
			tb_i(b, i->imm);
			tb_c(b, '\n');
		} else {
			if (i->imm) {
				tb_s(b, ", #");
				tb_i(b, i->imm);
			}
			tb_s(b, i->cond == 1 ? "]!\n" : "]\n");
		}
		return;
	case I_ADRP:
		tb_s(b, "\tadrp\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		ref_off(c, b, i->ref, i->off);
		tb_c(b, '\n');
		return;
	case I_ADDLO:
		tb_s(b, "\tadd\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		reg(b, i->rn, 1);
		tb_s(b, ", :lo12:");
		ref_off(c, b, i->ref, i->off);
		tb_c(b, '\n');
		return;
	case I_LDLO: case I_STLO:
		tb_c(b, '\t');
		tb_s(b, i->op == I_STLO ? st_name[0][i->sz] : ld_name[0][i->sz][i->cond]);
		tb_c(b, '\t');
		reg(b, i->rd, i->op == I_STLO ? i->sz == 3 : ld_x(i));
		tb_s(b, ", [");
		reg(b, i->rn, 1);
		tb_s(b, ", :lo12:");
		ref_off(c, b, i->ref, i->off);
		tb_s(b, "]\n");
		return;
	case I_LDGOT:
		tb_s(b, "\tadrp\t");
		reg(b, i->rd, 1);
		tb_s(b, ", :got:");
		ref_name(c, b, i->ref);
		tb_s(b, "\n\tldr\t");
		reg(b, i->rd, 1);
		tb_s(b, ", [");
		reg(b, i->rd, 1);
		tb_s(b, ", :got_lo12:");
		ref_name(c, b, i->ref);
		tb_s(b, "]\n");
		return;
	case I_JTE:
		tb_s(b, "\t.word\t");
		p_lbl(b, fid, i->label);
		tb_c(b, '-');
		p_lbl(b, fid, i->ref);
		tb_c(b, '\n');
		return;
	case I_ADR:
		tb_s(b, "\tadr\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_B:
		tb_s(b, "\tb\t");
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_BC:
		tb_s(b, "\tb.");
		tb_n8(b, cond_name[i->cond]);
		tb_c(b, '\t');
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_CBZ: case I_CBNZ:
		tb_s(b, i->op == I_CBZ ? "\tcbz\t" : "\tcbnz\t");
		reg(b, i->rd, i->sz);
		tb_s(b, ", ");
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_TBZ: case I_TBNZ:
		tb_s(b, i->op == I_TBZ ? "\ttbz\t" : "\ttbnz\t");
		reg(b, i->rd, i->sz);
		tb_s(b, ", #");
		tb_u(b, i->sh);
		tb_s(b, ", ");
		p_lbl(b, fid, i->label);
		tb_c(b, '\n');
		return;
	case I_TAIL:
		if (!i->cond)
			return;
		/* symbol quoting is identical for bl and b. */
		__attribute__((fallthrough));
	case I_BL: {
		tb_s(b, i->op == I_TAIL ? "\tb\t" : "\tbl\t");
		size_t at = b->n;

		ref_name(c, b, i->ref);
		if (reg_like(b->p + at, b->n - at)) {
			/* "bl b1" is a register operand to the assembler (llvm-mc too): quote it */
			size_t n = b->n - at;

			tb_c(b, '"');
			tb_c(b, '"');
			memmove(b->p + at + 1, b->p + at, n);
			b->p[at] = '"';
		}
		tb_c(b, '\n');
		return;
	}
	case I_BLR: case I_BR:
		tb_s(b, i->op == I_BLR ? "\tblr\t" : "\tbr\t");
		reg(b, i->rn, 1);
		tb_c(b, '\n');
		return;
	case I_RET:
		tb_s(b, "\tret\n");
		return;
	case I_BRK:
		tb_s(b, "\tbrk\t#");
		tb_hex(b, (uint64_t)i->imm);
		tb_c(b, '\n');
		return;
	case I_PRFM:
		tb_s(b, "\tprfm\tpldl1keep, [");
		reg(b, i->rn, 1);
		tb_s(b, "]\n");
		return;
	case I_MRS:
		tb_s(b, "\tmrs\t");
		reg(b, i->rd, 1);
		tb_s(b, ", ");
		tb_s(b, c->strs[i->ref].s);
		tb_c(b, '\n');
		return;
	case I_NOP:
		return;
	case I_WORD:
		tb_s(b, "\t.inst\t");
		tb_hex(b, (uint32_t)i->imm);
		tb_c(b, '\n');
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
			tb_s(b, q[k]);
			ref_name(c, b, i->ref);
		}
		for (int k = 0; q[k]; k++)
			if (!q[k + 1])
				tb_s(b, q[k]);
		return;
	}
	default:
		tb_s(b, "\t// ?\n");
		return;
	}
}
