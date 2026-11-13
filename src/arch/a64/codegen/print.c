/* vcc code generator: printer: records -> assembly text */
#include "../../../codegen/codegen-internal.h"

/* ---------------- printer: records -> assembly text ---------------- */

VPRIV void a64_print_label(tbuf *b, uint32_t fid, uint32_t l)
{
	vtext_string_append(b, ".LBB");
	vtext_hex_digits_append(b, fid);
	vtext_char_append(b, '_');
	vtext_hex_digits_append(b, l);
}

static void a64_print_move_immediate(tbuf *b, int rd, int x, uint64_t v)
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
	if (nz <= 1 || (chunks - nf) <= 1 || a64_logical_immediate_is_encodable(v, x)) {
		vtext_string_append(b, "\tmov\t");
		a64_register_name_append(b, rd, x);
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
		a64_register_name_append(b, rd, x);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, h);
		if (i) {
			vtext_string_append(b, ", lsl #");
			vtext_unsigned_append(b, (uint64_t)(16 * i));
		}
		vtext_char_append(b, '\n');
	}
	if (first)
		a64_print_move_immediate(b, rd, x, 0);
}

static const char a64_load_names[2][4][3][8] = {
	{ { "ldrb", "ldrsb", "ldrsb" }, { "ldrh", "ldrsh", "ldrsh" }, { "ldr", "ldrsw", "ldr" }, { "ldr", "ldr", "ldr" } },
	{ { "ldurb", "ldursb", "ldursb" }, { "ldurh", "ldursh", "ldursh" }, { "ldur", "ldursw", "ldur" }, { "ldur", "ldur", "ldur" } },
};
static const char a64_store_names[2][4][8] = { { "strb", "strh", "str", "str" }, { "sturb", "sturh", "stur", "stur" } };

static inline int a64_print_load_is_wide(const a64_record *i) { return i->sz == 3 || ((i->op == A64_OP_LD || i->op == A64_OP_LDX || i->op == A64_OP_LDLO) && i->cond == 1); }

static void a64_print_memory(tbuf *b, const a64_record *i)
{
	int64_t off = i->imm;
	int sz = i->sz, scaled, base = i->rn;
	int x = i->op == A64_OP_ST ? sz == 3 : a64_print_load_is_wide(i), v = i->op == A64_OP_LDF || i->op == A64_OP_STF;

	scaled = off >= 0 && (off & ((1 << sz) - 1)) == 0 && (off >> sz) < 4096;
	if (!scaled && !(off >= -256 && off < 256)) {
		a64_print_move_immediate(b, R_X17, 1, (uint64_t)off);
		vtext_string_append(b, "\tadd\tx17, ");
		a64_register_name_append(b, base, 1);
		vtext_string_append(b, ", x17\n");
		base = R_X17;
		off = 0;
		scaled = 1;
	}
	vtext_char_append(b, '\t');
	if (i->op == A64_OP_ST || i->op == A64_OP_STF)
		vtext_name_append(b, a64_store_names[!scaled][sz]);
	else
		vtext_name_append(b, a64_load_names[!scaled][sz][v ? 0 : i->cond]);
	vtext_char_append(b, '\t');
	if (v) {
		vtext_char_append(b, sz == 3 ? 'd' : 's');
		vtext_unsigned_append(b, i->rd);
	} else {
		a64_register_name_append(b, i->rd, x);
	}
	vtext_string_append(b, ", [");
	a64_register_name_append(b, base, 1);
	if (off) {
		vtext_string_append(b, ", #");
		vtext_signed_append(b, off);
	}
	vtext_string_append(b, "]\n");
}

static const char a64_alu_names[][8] = {
	[A64_OP_ADD] = "add", [A64_OP_SUB] = "sub", [A64_OP_ADDS] = "adds", [A64_OP_SUBS] = "subs", [A64_OP_AND] = "and",
	[A64_OP_ORR] = "orr", [A64_OP_EOR] = "eor", [A64_OP_ANDS] = "ands", [A64_OP_BIC] = "bic", [A64_OP_ORN] = "orn",
	[A64_OP_ADC] = "adc", [A64_OP_SBC] = "sbc", [A64_OP_ADCS] = "adcs", [A64_OP_SBCS] = "sbcs",
	[A64_OP_ADDI] = "add", [A64_OP_SUBI] = "sub", [A64_OP_ADDSI] = "adds", [A64_OP_SUBSI] = "subs",
	[A64_OP_ANDI] = "and", [A64_OP_ORRI] = "orr", [A64_OP_EORI] = "eor", [A64_OP_ANDSI] = "ands",
	[A64_OP_MADD] = "madd", [A64_OP_MSUB] = "msub", [A64_OP_SDIV] = "sdiv", [A64_OP_UDIV] = "udiv",
	[A64_OP_SMULH] = "smulh", [A64_OP_UMULH] = "umulh", [A64_OP_LSLV] = "lsl", [A64_OP_LSRV] = "lsr", [A64_OP_ASRV] = "asr",
	[A64_OP_SBFM] = "sbfm", [A64_OP_UBFM] = "ubfm", [A64_OP_BFM] = "bfm",
	[A64_OP_CSEL] = "csel", [A64_OP_CSINC] = "csinc", [A64_OP_CSINV] = "csinv", [A64_OP_CSNEG] = "csneg",
	[A64_OP_CLZ] = "clz", [A64_OP_CLS] = "cls", [A64_OP_RBIT] = "rbit", [A64_OP_REV] = "rev", [A64_OP_REV16] = "rev16", [A64_OP_REV32] = "rev32",
};

static void a64_print_three_registers(tbuf *b, const char *mn, const a64_record *i)
{
	vtext_char_append(b, '\t');
	vtext_name_append(b, mn);
	vtext_char_append(b, '\t');
	a64_register_name_append(b, i->rd, i->sz);
	vtext_string_append(b, ", ");
	a64_register_name_append(b, i->rn, i->sz);
	vtext_string_append(b, ", ");
	a64_register_name_append(b, i->rm, i->sz);
}

static void a64_print_add_immediate(tbuf *b, const a64_record *i)
{
	int op = i->op;
	int64_t v = i->imm;
	bool flags = op == A64_OP_ADDSI || op == A64_OP_SUBSI;
	bool sub = op == A64_OP_SUBI || op == A64_OP_SUBSI;

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
			a64_register_name_append(b, i->rd, i->sz);
			vtext_string_append(b, ", ");
			a64_register_name_append(b, rn, i->sz);
			vtext_string_append(b, ", #"), vtext_signed_append(b, v >> 12), vtext_string_append(b, ", lsl #12\n");
			rn = i->rd;
			v &= 0xfff;
		}
		vtext_char_append(b, '\t'), vtext_string_append(b, mn), vtext_char_append(b, '\t');
		a64_register_name_append(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, rn, i->sz);
		if ((v & 0xfff) == 0 && v)
			vtext_string_append(b, ", #"), vtext_signed_append(b, v >> 12), vtext_string_append(b, ", lsl #12\n");
		else
			vtext_string_append(b, ", #"), vtext_signed_append(b, v), vtext_char_append(b, '\n');
		return;
	}
	a64_print_move_immediate(b, R_X17, i->sz, (uint64_t)v);
	vtext_char_append(b, '\t'), vtext_string_append(b, sub ? (flags ? "subs" : "sub") : (flags ? "adds" : "add")), vtext_char_append(b, '\t');
	a64_register_name_append(b, i->rd, i->sz);
	vtext_string_append(b, ", ");
	a64_register_name_append(b, i->rn, i->sz);
	vtext_string_append(b, i->sz ? ", x17\n" : ", w17\n");
}

/* a register name (x0, w30, b1, q7, v31, sp, xzr, ...) */
static bool a64_print_text_is_register(const char *p, size_t n)
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

VPRIV void a64_record_print(cc *c, tbuf *b, const a64_record *i, uint32_t fid, const char *raw)
{
	switch (i->op) {
	case A64_OP_LABEL:
		a64_print_label(b, fid, i->label);
		vtext_string_append(b, ":\n");
		return;
	case A64_OP_RAW:
		if (i->cond)	/* inline asm between clang's markers (bench/vcc-fpcheck.py) */
			vtext_string_append(b, "\t//APP\n");
		vtext_append(b, raw + i->off, i->ref);
		vtext_char_append(b, '\n');
		if (i->cond)
			vtext_string_append(b, "\t//NO_APP\n");
		return;
	case A64_OP_ADD: case A64_OP_SUB: case A64_OP_ADDS: case A64_OP_SUBS: case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR:
	case A64_OP_ANDS: case A64_OP_BIC: case A64_OP_ORN: case A64_OP_ADC: case A64_OP_SBC: case A64_OP_ADCS: case A64_OP_SBCS:
		if (i->cond >= 8) {	/* extended register */
			static const char xn[8][5] = { "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx" };
			int o = i->cond - 8;

			vtext_char_append(b, '\t'), vtext_name_append(b, a64_alu_names[i->op]), vtext_char_append(b, '\t');
			a64_register_name_append(b, i->rd, i->sz), vtext_string_append(b, ", ");
			a64_register_name_append(b, i->rn, i->sz), vtext_string_append(b, ", ");
			a64_register_name_append(b, i->rm, (o & 3) == 3), vtext_string_append(b, ", "), vtext_string_append(b, xn[o]);
			if (i->sh)
				vtext_string_append(b, " #"), vtext_unsigned_append(b, i->sh);
			vtext_char_append(b, '\n');
			return;
		}
		a64_print_three_registers(b, a64_alu_names[i->op], i);
		if (i->sh) {
			vtext_string_append(b, i->cond == 0 ? ", lsl #" : i->cond == 1 ? ", lsr #" : ", asr #");
			vtext_unsigned_append(b, i->sh);
		}
		vtext_char_append(b, '\n');
		return;
	case A64_OP_ADDI: case A64_OP_SUBI: case A64_OP_ADDSI: case A64_OP_SUBSI:
		a64_print_add_immediate(b, i);
		return;
	case A64_OP_ANDI: case A64_OP_ORRI: case A64_OP_EORI: case A64_OP_ANDSI:
		vtext_char_append(b, '\t');
		vtext_name_append(b, a64_alu_names[i->op]);
		vtext_char_append(b, '\t');
		a64_register_name_append(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rn, i->sz);
		vtext_string_append(b, ", #");
		vtext_hex_prefixed_append(b, i->sz ? (uint64_t)i->imm : (uint32_t)i->imm);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_MOVI:
		a64_print_move_immediate(b, i->rd, i->sz, (uint64_t)i->imm);
		return;
	case A64_OP_MOV:
		vtext_string_append(b, "\tmov\t");
		a64_register_name_append(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rn, i->sz);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_MADD: case A64_OP_MSUB:
		a64_print_three_registers(b, a64_alu_names[i->op], i);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->ra, i->sz);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_SDIV: case A64_OP_UDIV: case A64_OP_SMULH: case A64_OP_UMULH: case A64_OP_LSLV: case A64_OP_LSRV: case A64_OP_ASRV:
		a64_print_three_registers(b, a64_alu_names[i->op], i);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_SBFM: case A64_OP_UBFM: case A64_OP_BFM:
		vtext_char_append(b, '\t');
		vtext_name_append(b, a64_alu_names[i->op]);
		vtext_char_append(b, '\t');
		a64_register_name_append(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rn, i->sz);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, (uint64_t)i->imm);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, i->sh);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_CSEL: case A64_OP_CSINC: case A64_OP_CSINV: case A64_OP_CSNEG:
		a64_print_three_registers(b, a64_alu_names[i->op], i);
		vtext_string_append(b, ", ");
		vtext_name_append(b, a64_condition_names[i->cond]);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_CLZ: case A64_OP_CLS: case A64_OP_RBIT: case A64_OP_REV: case A64_OP_REV16: case A64_OP_REV32:
		vtext_char_append(b, '\t');
		vtext_name_append(b, a64_alu_names[i->op]);
		vtext_char_append(b, '\t');
		a64_register_name_append(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rn, i->sz);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_LD: case A64_OP_ST: case A64_OP_LDF: case A64_OP_STF:
		a64_print_memory(b, i);
		return;
	case A64_OP_SMADDL: case A64_OP_UMADDL:
		vtext_string_append(b, i->op == A64_OP_SMADDL ? "\tsmaddl\t" : "\tumaddl\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rn, 0);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rm, 0);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->ra, 1);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_LDX: case A64_OP_STX:
		vtext_char_append(b, '\t');
		vtext_name_append(b, i->op == A64_OP_STX ? a64_store_names[0][i->sz] : a64_load_names[0][i->sz][i->cond]);
		vtext_char_append(b, '\t');
		a64_register_name_append(b, i->rd, i->op == A64_OP_STX ? i->sz == 3 : a64_print_load_is_wide(i));
		vtext_string_append(b, ", [");
		a64_register_name_append(b, i->rn, 1);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rm, i->ra == 3);
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
	case A64_OP_LDP: case A64_OP_STP:
		vtext_string_append(b, i->op == A64_OP_LDP ? "\tldp\t" : "\tstp\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->ra, 1);
		vtext_string_append(b, ", [");
		a64_register_name_append(b, i->rn, 1);
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
	case A64_OP_ADRP:
		vtext_string_append(b, "\tadrp\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", ");
		codegen_reference_offset_append(c, b, i->ref, i->off);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_ADDLO:
		vtext_string_append(b, "\tadd\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", ");
		a64_register_name_append(b, i->rn, 1);
		vtext_string_append(b, ", :lo12:");
		codegen_reference_offset_append(c, b, i->ref, i->off);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_LDLO: case A64_OP_STLO:
		vtext_char_append(b, '\t');
		vtext_string_append(b, i->op == A64_OP_STLO ? a64_store_names[0][i->sz] : a64_load_names[0][i->sz][i->cond]);
		vtext_char_append(b, '\t');
		a64_register_name_append(b, i->rd, i->op == A64_OP_STLO ? i->sz == 3 : a64_print_load_is_wide(i));
		vtext_string_append(b, ", [");
		a64_register_name_append(b, i->rn, 1);
		vtext_string_append(b, ", :lo12:");
		codegen_reference_offset_append(c, b, i->ref, i->off);
		vtext_string_append(b, "]\n");
		return;
	case A64_OP_LDGOT:
		vtext_string_append(b, "\tadrp\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", :got:");
		codegen_reference_name_append(c, b, i->ref);
		vtext_string_append(b, "\n\tldr\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", [");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", :got_lo12:");
		codegen_reference_name_append(c, b, i->ref);
		vtext_string_append(b, "]\n");
		return;
	case A64_OP_JTE:
		vtext_string_append(b, "\t.word\t");
		a64_print_label(b, fid, i->label);
		vtext_char_append(b, '-');
		a64_print_label(b, fid, i->ref);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_ADR:
		vtext_string_append(b, "\tadr\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", ");
		a64_print_label(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_B:
		vtext_string_append(b, "\tb\t");
		a64_print_label(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_BC:
		vtext_string_append(b, "\tb.");
		vtext_name_append(b, a64_condition_names[i->cond]);
		vtext_char_append(b, '\t');
		a64_print_label(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_CBZ: case A64_OP_CBNZ:
		vtext_string_append(b, i->op == A64_OP_CBZ ? "\tcbz\t" : "\tcbnz\t");
		a64_register_name_append(b, i->rd, i->sz);
		vtext_string_append(b, ", ");
		a64_print_label(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_TBZ: case A64_OP_TBNZ:
		vtext_string_append(b, i->op == A64_OP_TBZ ? "\ttbz\t" : "\ttbnz\t");
		a64_register_name_append(b, i->rd, i->sz);
		vtext_string_append(b, ", #");
		vtext_unsigned_append(b, i->sh);
		vtext_string_append(b, ", ");
		a64_print_label(b, fid, i->label);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_TAIL:
		if (!i->cond)
			return;
		/* symbol quoting is identical for bl and b. */
		__attribute__((fallthrough));
	case A64_OP_BL: {
		vtext_string_append(b, i->op == A64_OP_TAIL ? "\tb\t" : "\tbl\t");
		size_t at = b->n;

		codegen_reference_name_append(c, b, i->ref);
		if (a64_print_text_is_register(b->p + at, b->n - at)) {
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
	case A64_OP_BLR: case A64_OP_BR:
		vtext_string_append(b, i->op == A64_OP_BLR ? "\tblr\t" : "\tbr\t");
		a64_register_name_append(b, i->rn, 1);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_RET:
		vtext_string_append(b, "\tret\n");
		return;
	case A64_OP_BRK:
		vtext_string_append(b, "\tbrk\t#");
		vtext_hex_prefixed_append(b, (uint64_t)i->imm);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_PRFM:
		vtext_string_append(b, "\tprfm\tpldl1keep, [");
		a64_register_name_append(b, i->rn, 1);
		vtext_string_append(b, "]\n");
		return;
	case A64_OP_MRS:
		vtext_string_append(b, "\tmrs\t");
		a64_register_name_append(b, i->rd, 1);
		vtext_string_append(b, ", ");
		vtext_string_append(b, c->strs[i->ref].s);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_NOP:
		return;
	case A64_OP_WORD:
		vtext_string_append(b, "\t.inst\t");
		vtext_hex_prefixed_append(b, (uint32_t)i->imm);
		vtext_char_append(b, '\n');
		return;
	case A64_OP_TLV: {
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
			codegen_reference_name_append(c, b, i->ref);
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
