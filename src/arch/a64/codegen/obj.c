/* vcc code generator: object backend: records -> machine code */
#include "../../../codegen/codegen-internal.h"

static uint32_t a64_commit_label(codegen_commit_context *R, uint32_t fid, uint32_t l)
{
	if (R->label_serials[l] == R->serial)
		return R->label_symbols[l];
	R->name_buffer.n = 0;
	a64_print_label(&R->name_buffer, fid, l);
	R->label_serials[l] = R->serial;
	return R->label_symbols[l] = as_symbol_by_name(R->assembler, R->name_buffer.p, R->name_buffer.n);
}

static unsigned a64_encode_move_immediate(uint32_t *w, int rd, int x, uint64_t v)
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
	if (nz <= 1 || (chunks - nf) <= 1 || a64_logical_immediate_is_encodable(v, x)) {
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

static inline unsigned a64_encode_register_field(int r) { return r == SP ? 31u : (unsigned)r & 31; }	/* SP and ZR are both 31 */

/* add/sub (register), the extended form when sp is involved (as.c addsub_reg) */
static uint32_t a64_encode_add_subtract_register(int sf, int op, int s, int rd, int rn, int rm, unsigned kind, unsigned amt)
{
	if (kind >= 8)	/* extended register: option kind - 8 */
		return 0x0b200000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | a64_encode_register_field(rm) << 16 |
		       (kind - 8) << 13 | (amt & 7) << 10 | a64_encode_register_field(rn) << 5 | a64_encode_register_field(rd);
	if (rd == SP || rn == SP)
		return 0x0b200000u | (uint32_t)sf << 31 | (uint32_t)op << 30 | (uint32_t)s << 29 | a64_encode_register_field(rm) << 16 |
		       (sf ? 3u : 2u) << 13 | (amt & 7) << 10 | a64_encode_register_field(rn) << 5 | a64_encode_register_field(rd);
	return a64_addsub_reg(sf, op, s, a64_encode_register_field(rd), a64_encode_register_field(rn), a64_encode_register_field(rm), kind, amt);
}

static const uint8_t a64_load_opcodes[4][3] = { { 1, 2, 3 }, { 1, 2, 3 }, { 1, 2, 1 }, { 1, 1, 1 } };

/* encode one record into w (returns the word count; the fixup, if any, in *f) */
static unsigned a64_encode_record(const a64_record *i, uint32_t *w, codegen_commit_fixup *f, const uint32_t *loff, uint32_t at)
{
	int sf = i->size;

	f->type = 0;
	switch (i->opcode) {
	case A64_OP_LABEL: case A64_OP_RAW: case A64_OP_NOP: case I_MOVSP:
		return 0;
	case A64_OP_ADD: case A64_OP_SUB: case A64_OP_ADDS: case A64_OP_SUBS:
		w[0] = a64_encode_add_subtract_register(sf, i->opcode == A64_OP_SUB || i->opcode == A64_OP_SUBS, i->opcode == A64_OP_ADDS || i->opcode == A64_OP_SUBS,
				    i->rd, i->rn, i->rm, i->shift || i->condition >= 8 ? i->condition : 0, i->shift);
		return 1;
	case A64_OP_AND: case A64_OP_ORR: case A64_OP_EOR: case A64_OP_ANDS: case A64_OP_BIC: case A64_OP_ORN: {
		static const uint8_t opc[] = { [A64_OP_AND] = 0, [A64_OP_ORR] = 1, [A64_OP_EOR] = 2, [A64_OP_ANDS] = 3, [A64_OP_BIC] = 0, [A64_OP_ORN] = 1 };

		w[0] = a64_logic_reg(sf, opc[i->opcode], i->opcode == A64_OP_BIC || i->opcode == A64_OP_ORN, a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), a64_encode_register_field(i->rm),
				     i->shift ? i->condition : 0, i->shift);
		return 1;
	}
	case A64_OP_ADC: case A64_OP_SBC: case A64_OP_ADCS: case A64_OP_SBCS: {
		static const uint32_t b[] = { [A64_OP_ADC] = 0x1a000000, [A64_OP_SBC] = 0x5a000000, [A64_OP_ADCS] = 0x3a000000, [A64_OP_SBCS] = 0x7a000000 };

		w[0] = b[i->opcode] | (uint32_t)sf << 31 | a64_encode_register_field(i->rm) << 16 | a64_encode_register_field(i->rn) << 5 | a64_encode_register_field(i->rd);
		return 1;
	}
	case A64_OP_ADDI: case A64_OP_SUBI: case A64_OP_ADDSI: case A64_OP_SUBSI: {
		int64_t v = i->immediate;
		bool flags = i->opcode == A64_OP_ADDSI || i->opcode == A64_OP_SUBSI;
		bool sub = i->opcode == A64_OP_SUBI || i->opcode == A64_OP_SUBSI;
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
				w[n++] = a64_addsub_imm(sf, sub, flags, a64_encode_register_field(i->rd), a64_encode_register_field(rn), (unsigned)(v >> 12), 1);
				rn = i->rd;
				v &= 0xfff;
			}
			if ((v & 0xfff) == 0 && v)
				w[n++] = a64_addsub_imm(sf, sub, flags, a64_encode_register_field(i->rd), a64_encode_register_field(rn), (unsigned)(v >> 12), 1);
			else
				w[n++] = a64_addsub_imm(sf, sub, flags, a64_encode_register_field(i->rd), a64_encode_register_field(rn), (unsigned)v, 0);
			return n;
		}
		n = a64_encode_move_immediate(w, R_X17, sf, (uint64_t)v);
		w[n] = a64_encode_add_subtract_register(sf, sub, flags, i->rd, i->rn, R_X17, 0, 0);
		return n + 1;
	}
	case A64_OP_ANDI: case A64_OP_ORRI: case A64_OP_EORI: case A64_OP_ANDSI: {
		static const uint8_t opc[] = { [A64_OP_ANDI] = 0, [A64_OP_ORRI] = 1, [A64_OP_EORI] = 2, [A64_OP_ANDSI] = 3 };
		uint64_t v = sf ? (uint64_t)i->immediate : (uint32_t)i->immediate;

		w[0] = a64_logic_imm(sf, opc[i->opcode], a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), (unsigned)a64_logimm(v, sf));
		return 1;
	}
	case A64_OP_MOVI:
		return a64_encode_move_immediate(w, i->rd, sf, (uint64_t)i->immediate);
	case A64_OP_MOV:
		if (i->rd == SP || i->rn == SP)
			w[0] = a64_addsub_imm(sf, 0, 0, a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), 0, 0);
		else
			w[0] = a64_logic_reg(sf, 1, 0, a64_encode_register_field(i->rd), 31, a64_encode_register_field(i->rn), 0, 0);
		return 1;
	case A64_OP_MADD: case A64_OP_MSUB:
		w[0] = a64_madd(sf, i->opcode == A64_OP_MSUB, a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), a64_encode_register_field(i->rm), a64_encode_register_field(i->ra));
		return 1;
	case A64_OP_SDIV: case A64_OP_UDIV: case A64_OP_LSLV: case A64_OP_LSRV: case A64_OP_ASRV: {
		static const uint8_t opc[] = { [A64_OP_SDIV] = 3, [A64_OP_UDIV] = 2, [A64_OP_LSLV] = 8, [A64_OP_LSRV] = 9, [A64_OP_ASRV] = 10 };

		w[0] = a64_dp2(sf, opc[i->opcode], a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), a64_encode_register_field(i->rm));
		return 1;
	}
	case A64_OP_SMULH: case A64_OP_UMULH:
		w[0] = (i->opcode == A64_OP_SMULH ? 0x9b407c00u : 0x9bc07c00u) | a64_encode_register_field(i->rm) << 16 | a64_encode_register_field(i->rn) << 5 | a64_encode_register_field(i->rd);
		return 1;
	case A64_OP_SBFM: case A64_OP_UBFM: case A64_OP_BFM:
		w[0] = a64_bfm(sf, i->opcode == A64_OP_SBFM ? 0 : i->opcode == A64_OP_BFM ? 1 : 2, a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), (unsigned)i->immediate, i->shift);
		return 1;
	case A64_OP_CSEL: case A64_OP_CSINC: case A64_OP_CSINV: case A64_OP_CSNEG:
		w[0] = a64_csel(sf, i->opcode == A64_OP_CSINV || i->opcode == A64_OP_CSNEG, i->opcode == A64_OP_CSINC || i->opcode == A64_OP_CSNEG,
				a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), a64_encode_register_field(i->rm), i->condition);
		return 1;
	case A64_OP_CLZ: case A64_OP_CLS: case A64_OP_RBIT: case A64_OP_REV: case A64_OP_REV16: case A64_OP_REV32: {
		uint32_t b;

		switch (i->opcode) {
		case A64_OP_CLZ: b = 0x5ac01000u | (uint32_t)sf << 31; break;
		case A64_OP_CLS: b = 0x5ac01400u | (uint32_t)sf << 31; break;
		case A64_OP_RBIT: b = 0x5ac00000u | (uint32_t)sf << 31; break;
		case A64_OP_REV: b = 0x5ac00800u | (uint32_t)sf << 31 | (sf ? 0x400u : 0); break;
		case A64_OP_REV16: b = 0x5ac00400u | (uint32_t)sf << 31; break;
		default: b = 0xdac00800u; break;
		}
		w[0] = b | a64_encode_register_field(i->rn) << 5 | a64_encode_register_field(i->rd);
		return 1;
	}
	case A64_OP_LD: case A64_OP_ST: case A64_OP_LDF: case A64_OP_STF: {
		int64_t off = i->immediate;
		int sz = i->size, base = i->rn, v = i->opcode == A64_OP_LDF || i->opcode == A64_OP_STF;
		unsigned n = 0, opc = i->opcode == A64_OP_ST || i->opcode == A64_OP_STF ? 0 : v ? 1 : a64_load_opcodes[sz][i->condition];
		bool scaled = off >= 0 && (off & ((1 << sz) - 1)) == 0 && (off >> sz) < 4096;

		if (!scaled && !(off >= -256 && off < 256)) {
			n = a64_encode_move_immediate(w, R_X17, 1, (uint64_t)off);
			w[n++] = a64_encode_add_subtract_register(1, 0, 0, R_X17, base, R_X17, 0, 0);
			base = R_X17;
			off = 0;
			scaled = true;
		}
		w[n++] = scaled ? a64_ldst_uoff((unsigned)sz, v, opc, a64_encode_register_field(i->rd), a64_encode_register_field(base), (unsigned)(off >> sz))
				: a64_ldst_imm9((unsigned)sz, v, opc, 0, a64_encode_register_field(i->rd), a64_encode_register_field(base), (int)off);
		return n;
	}
	case A64_OP_SMADDL: case A64_OP_UMADDL:
		w[0] = (i->opcode == A64_OP_SMADDL ? 0x9b200000u : 0x9ba00000u) | a64_encode_register_field(i->rm) << 16 | a64_encode_register_field(i->ra) << 10 | a64_encode_register_field(i->rn) << 5 |
		       a64_encode_register_field(i->rd);
		return 1;
	case A64_OP_LDX: case A64_OP_STX:	/* register offset */
		w[0] = (uint32_t)i->size << 30 | 0x38200800u | (i->opcode == A64_OP_STX ? 0 : a64_load_opcodes[i->size][i->condition]) << 22 |
		       a64_encode_register_field(i->rm) << 16 | (uint32_t)i->ra << 13 | (i->shift ? 1u << 12 : 0) | a64_encode_register_field(i->rn) << 5 | a64_encode_register_field(i->rd);
		return 1;
	case A64_OP_LDP: case A64_OP_STP:
		w[0] = a64_ldstp(2, 0, i->condition == 0 ? 2 : i->condition == 1 ? 3 : 1, i->opcode == A64_OP_LDP, a64_encode_register_field(i->rd), a64_encode_register_field(i->ra), a64_encode_register_field(i->rn),
				 (int)(i->immediate / 8));
		return 1;
	case A64_OP_ADRP:
		w[0] = 0x90000000u | a64_encode_register_field(i->rd);
		*f = (codegen_commit_fixup){ 0, 0, R_AARCH64_ADR_PREL_PG_HI21, i->reference, i->offset };
		return 1;
	case A64_OP_ADDLO:
		w[0] = a64_addsub_imm(1, 0, 0, a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), 0, 0);
		*f = (codegen_commit_fixup){ 0, 0, R_AARCH64_ADD_ABS_LO12_NC, i->reference, i->offset };
		return 1;
	case A64_OP_LDLO: case A64_OP_STLO: {
		static const uint32_t lo12[] = { R_AARCH64_LDST8_ABS_LO12_NC, R_AARCH64_LDST16_ABS_LO12_NC,
						 R_AARCH64_LDST32_ABS_LO12_NC, R_AARCH64_LDST64_ABS_LO12_NC };
		unsigned opc = i->opcode == A64_OP_STLO ? 0 : a64_load_opcodes[i->size][i->condition];

		w[0] = a64_ldst_uoff(i->size, 0, opc, a64_encode_register_field(i->rd), a64_encode_register_field(i->rn), 0);
		*f = (codegen_commit_fixup){ 0, 0, lo12[i->size], i->reference, i->offset };
		return 1;
	}
	case A64_OP_LDGOT:
		/* two fixups: the caller handles A64_OP_LDGOT's second word */
		w[0] = 0x90000000u | a64_encode_register_field(i->rd);
		w[1] = a64_ldst_uoff(3, 0, 1, a64_encode_register_field(i->rd), a64_encode_register_field(i->rd), 0);
		*f = (codegen_commit_fixup){ 0, 0, R_AARCH64_ADR_GOT_PAGE, i->reference, 0 };
		return 2;
	case A64_OP_ADR:
		if (loff) {
			w[0] = a64_adr(a64_encode_register_field(i->rd), ((int64_t)loff[i->label] - at) * 4);
			return 1;
		}
		w[0] = 0x10000000u | a64_encode_register_field(i->rd);
		*f = (codegen_commit_fixup){ 0, 1, R_AARCH64_ADR_PREL_LO21, i->label, 0 };
		return 1;
	case A64_OP_JTE:
		if (loff) {
			w[0] = (uint32_t)(((int64_t)loff[i->label] - loff[i->reference]) * 4);
			return 1;
		}
		w[0] = 0;
		*f = (codegen_commit_fixup){ 0, 1, R_AARCH64_ADR_PREL_LO21, i->label, 0 };
		return 1;
	case A64_OP_B: case A64_OP_BC: case A64_OP_CBZ: case A64_OP_CBNZ: case A64_OP_TBZ: case A64_OP_TBNZ: {
		int64_t d = loff ? ((int64_t)loff[i->label] - at) * 4 : 0;

		switch (i->opcode) {
		case A64_OP_B: w[0] = a64_b(d); break;
		case A64_OP_BC: w[0] = a64_bcond(i->condition, d); break;
		case A64_OP_TBZ: case A64_OP_TBNZ: w[0] = a64_tbz(i->opcode == A64_OP_TBNZ, a64_encode_register_field(i->rd), i->shift, d); break;
		default: w[0] = a64_cbz(sf, i->opcode == A64_OP_CBNZ, a64_encode_register_field(i->rd), d); break;
		}
		if (!loff)
			*f = (codegen_commit_fixup){ 0, 1, i->opcode == A64_OP_B ? R_AARCH64_JUMP26 : i->opcode == A64_OP_TBZ || i->opcode == A64_OP_TBNZ ? R_AARCH64_TSTBR14 :
					   R_AARCH64_CONDBR19, i->label, 0 };
		return 1;
	}
	case A64_OP_TAIL:
		if (!i->condition)
			return 0;
		w[0] = 0x14000000u;
		*f = (codegen_commit_fixup){ 0, 0, R_AARCH64_JUMP26, i->reference, 0 };
		return 1;
	case A64_OP_BL:
		w[0] = 0x94000000u;
		*f = (codegen_commit_fixup){ 0, 0, R_AARCH64_CALL26, i->reference, 0 };
		return 1;
	case A64_OP_BLR:
		w[0] = a64_blr(a64_encode_register_field(i->rn));
		return 1;
	case A64_OP_BR:
		w[0] = a64_br(a64_encode_register_field(i->rn));
		return 1;
	case A64_OP_RET:
		w[0] = a64_ret(30);
		return 1;
	case A64_OP_BRK:
		w[0] = 0xd4200000u | ((uint32_t)i->immediate & 0xffff) << 5;
		return 1;
	case A64_OP_PRFM:
		w[0] = 0xf9800000u | a64_encode_register_field(i->rn) << 5;	/* prfm pldl1keep */
		return 1;
	case A64_OP_MRS:
		w[0] = 0;	/* the caller looks the register up */
		return 1;
	case A64_OP_WORD:
		w[0] = (uint32_t)i->immediate;
		return 1;
	case A64_OP_TLV:
		switch (i->condition) {
		case A64_TLS_LOCAL_EXEC:	/* mrs x1, tpidr_el0; add x0, x1, :tprel_hi12:; add x0, x0, :tprel_lo12_nc: */
			w[0] = 0xd53bd041u;
			w[1] = 0x91400020u;
			w[2] = 0x91000000u;
			*f = (codegen_commit_fixup){ 1, 0, R_AARCH64_TLSLE_ADD_TPREL_HI12, i->reference, 0 };
			return 3;
		case A64_TLS_INITIAL_EXEC:	/* mrs x1, tpidr_el0; adrp x0, :gottprel:; ldr x0, [x0, :gottprel_lo12:]; add x0, x1, x0 */
			w[0] = 0xd53bd041u;
			w[1] = 0x90000000u;
			w[2] = a64_ldst_uoff(3, 0, 1, 0, 0, 0);
			w[3] = 0x8b000020u;
			*f = (codegen_commit_fixup){ 1, 0, R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21, i->reference, 0 };
			return 4;
		case A64_TLS_DESCRIPTOR:	/* adrp x0; ldr x1, [x0]; add x0, x0; blr x1 (.tlsdesccall); mrs x1, tpidr_el0; add x0, x1, x0 */
			w[0] = 0x90000000u;
			w[1] = a64_ldst_uoff(3, 0, 1, 1, 0, 0);
			w[2] = 0x91000000u;
			w[3] = a64_blr(1);
			w[4] = 0xd53bd041u;
			w[5] = 0x8b000020u;
			*f = (codegen_commit_fixup){ 0, 0, R_AARCH64_TLSDESC_ADR_PAGE21, i->reference, 0 };
			return 6;
		}
		/* adrp x0, ref@TLVPPAGE; ldr x0, [x0, ref@TLVPPAGEOFF]; ldr x16, [x0]; blr x16 */
		w[0] = 0x90000000u;
		w[1] = a64_ldst_uoff(3, 0, 1, 0, 0, 0);
		w[2] = a64_ldst_uoff(3, 0, 1, 16, 0, 0);
		w[3] = a64_blr(16);
		*f = (codegen_commit_fixup){ 0, 0, R_VCC_TLVP_PAGE21, i->reference, 0 };
		return 4;
	}
	return 0;
}

/* an upper bound on the words record i takes (inline asm: unknown, a lot) */
VPRIV uint32_t a64_record_word_count(const a64_record *i)
{
	uint32_t w[16];
	codegen_commit_fixup f;

	return i->opcode == A64_OP_RAW ? 1u << 20 : a64_encode_record(i, w, &f, NULL, 0);
}

/* the fixups after an instruction's first: off is where the instruction starts */
static void a64_commit_fixups_add(ElfObj *o, uint32_t frag, uint64_t off, uint8_t *p, const a64_record *i, uint32_t y, as_context *as)
{
	if (i->opcode == A64_OP_LDGOT)
		elf_fixup(o, frag, off + 4, p + 4, R_AARCH64_LD64_GOT_LO12_NC, as_symbol_value(as, y, 0));
	else if (i->opcode != A64_OP_TLV)
		return;
	else if (i->condition == 0)
		elf_fixup(o, frag, off + 4, p + 4, R_VCC_TLVP_PAGEOFF12, as_symbol_value(as, y, 0));
	else if (i->condition == A64_TLS_LOCAL_EXEC)
		elf_fixup(o, frag, off + 8, p + 8, R_AARCH64_TLSLE_ADD_TPREL_LO12_NC, as_symbol_value(as, y, 0));
	else if (i->condition == A64_TLS_INITIAL_EXEC)
		elf_fixup(o, frag, off + 8, p + 8, R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC, as_symbol_value(as, y, 0));
	else {
		elf_fixup(o, frag, off + 4, p + 4, R_AARCH64_TLSDESC_LD64_LO12, as_symbol_value(as, y, 0));
		elf_fixup(o, frag, off + 8, p + 8, R_AARCH64_TLSDESC_ADD_LO12, as_symbol_value(as, y, 0));
		elf_fixup(o, frag, off + 12, p + 12, R_AARCH64_TLSDESC_CALL, as_symbol_value(as, y, 0));
	}
}

/* emit one instruction's words at the end of frag, with its fixup(s) */
static void a64_commit_words_add(codegen_commit_context *R, uint32_t frag, const uint32_t *w, unsigned n, const codegen_commit_fixup *f, uint32_t fid, const a64_record *i)
{
	ElfObj *o = R->object;
	uint64_t off;
	uint8_t *p;

	elf_code(o, frag);
	off = elf_frag(o, frag)->size;
	p = elf_grow(o, frag, (size_t)n * 4);
	for (unsigned k = 0; k < n; k++)
		memcpy(p + 4 * k, &w[k], 4);
	if (f->type) {
		uint32_t y = f->label_flag ? a64_commit_label(R, fid, f->reference) : codegen_commit_symbol_find(R, f->reference);

		elf_fixup(o, frag, off + 4u * f->word_index, p + 4 * f->word_index, f->type, as_symbol_value(R->assembler, y, f->offset));
		a64_commit_fixups_add(o, frag, off, p, i, y, R->assembler);
	}
}

static inline int a64_hex_digit_value(char ch)
{
	return ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;	/* vtext_hex_digits_append: lower case */
}

/* per label of a function with inline asm (valid if ser == rctx.ser) */
enum { A64_COMMIT_LABEL_NAMED = 1, A64_COMMIT_LABEL_PLACED = 2, A64_COMMIT_LABEL_SYMBOL_DEFINED = 4 };

static inline codegen_commit_label *a64_commit_label_state(codegen_commit_context *R, uint32_t l)
{
	codegen_commit_label *L = &R->labels[l];

	if (L->serial != R->serial)
		*L = (codegen_commit_label){ R->serial, 0, 0, 0, 0 };
	return L;
}

/* label-relative ranges, in words: b.cond/cbz/cbnz imm19, adr 21 bits of bytes, b imm26 */
#define A64_BRANCH_IMMEDIATE19_MAX ((1 << 18) - 1)
#define A64_BRANCH_IMMEDIATE19_MIN (-(1 << 18))

/* the branch record i at (frag, off, p) to placed label L */
static bool a64_commit_branch(codegen_commit_context *R, uint32_t fid, const a64_record *i, codegen_commit_label *L, uint32_t frag, uint32_t off, uint8_t *p)
{
	uint32_t w[4];
	codegen_commit_fixup f;

	if (L->fragment == frag && !(L->offset & 3) && !(off & 3)) {
		int64_t d = ((int64_t)L->offset - off) / 4;

		if ((i->opcode == A64_OP_TBZ || i->opcode == A64_OP_TBNZ) && (d < -(1 << 13) || d >= 1 << 13))
			return codegen_commit_fail(R, "function with inline asm too large: tbz beyond 32 KB") == 0;
		if (i->opcode != A64_OP_B && (d < A64_BRANCH_IMMEDIATE19_MIN || d > A64_BRANCH_IMMEDIATE19_MAX))	/* not relaxed with inline asm in the function */
			return codegen_commit_fail(R, "function with inline asm too large: conditional branch or adr beyond 1 MB") == 0;
		R->label_word_offsets[i->label] = L->offset / 4;
		a64_encode_record(i, w, &f, R->label_word_offsets, off / 4);
		memcpy(p, w, 4);
		return true;
	}
	{
		uint32_t y = a64_commit_label(R, fid, i->label);

		if (!(L->flags & A64_COMMIT_LABEL_SYMBOL_DEFINED)) {
			if (elf_symp(R->object, y)->flags & ESYM_DEFINED)
				return codegen_commit_fail(R, "invalid symbol redefinition") == 0;
			elf_define(R->object, y, L->fragment, L->offset);
			L->flags |= A64_COMMIT_LABEL_SYMBOL_DEFINED;
		}
		a64_encode_record(i, w, &f, NULL, 0);
		memcpy(p, w, 4);
		elf_fixup(R->object, frag, off, p, f.type, as_symbol_value(R->assembler, y, 0));
	}
	return true;
}

static inline bool a64_record_is_compare_branch(unsigned op) { return op == A64_OP_BC || op == A64_OP_CBZ || op == A64_OP_CBNZ; }

/* first rpatch with at >= pos (pt is sorted by at) */
static uint32_t a64_commit_patch_lower_bound(const codegen_commit_patch *pt, uint32_t npt, uint32_t pos)
{
	uint32_t lo = 0, hi = npt;

	while (lo < hi) {
		uint32_t m = lo + (hi - lo) / 2;

		if (pt[m].word_position < pos)
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
static uint32_t a64_commit_relax(const a64_record *in, uint32_t nin, uint32_t *wb, uint32_t at, uint32_t *loff, codegen_commit_patch *pt, uint32_t npt)
{
	static _Thread_local uint32_t *cum;	/* cum[j]: relaxed branches among pt[0..j) */
	uint32_t nl = 0, s;
	bool grew = true;

	if (!cum)
		cum = codegen_memory_reserve((size_t)1 << 32);
	while (grew) {
		grew = false;
		cum[0] = 0;
		for (uint32_t j = 0; j < npt; j++)
			cum[j + 1] = cum[j] + pt[j].is_relaxed;
		for (uint32_t j = 0; j < npt; j++) {
			const a64_record *i = &in[pt[j].record_index];
			uint32_t t;
			int64_t d;

			if (pt[j].is_relaxed || !pt[j].fixup.label_flag || !a64_record_is_compare_branch(i->opcode))
				continue;
			t = loff[i->label];
			d = (int64_t)t + cum[a64_commit_patch_lower_bound(pt, npt, t)] - ((int64_t)pt[j].word_position + cum[j]);
			if (d < A64_BRANCH_IMMEDIATE19_MIN || d > A64_BRANCH_IMMEDIATE19_MAX) {
				pt[j].is_relaxed = 1;
				grew = true;
			}
		}
	}
	nl = cum[npt];
	if (!nl)
		return 0;
	/* spread the words: everything after a relaxed branch moves by the relaxed ones up to it */
	for (uint32_t j = npt, end_ = at; j-- > 0; ) {
		uint32_t b = pt[j].word_position;

		if (!pt[j].is_relaxed)
			continue;
		s = cum[j + 1];
		memmove(wb + b + 1 + s, wb + b + 1, (size_t)(end_ - b - 1) * 4);
		wb[b + s - 1] = wb[b];
		end_ = b;
	}
	for (uint32_t k = 0; k < nin; k++)
		if (in[k].opcode == A64_OP_LABEL)
			loff[in[k].label] += cum[a64_commit_patch_lower_bound(pt, npt, loff[in[k].label])];
	for (uint32_t j = 0; j < npt; j++)
		pt[j].word_position += cum[j];
	return nl;
}

VPRIV int a64_function_commit(codegen_commit_context *R, const codegen_code_header *h)
{
	const a64_record *in = (const a64_record *)(h + 1);
	const char *raw = (const char *)(in + h->record_count);
	uint32_t w[16];
	codegen_commit_fixup f;

	if (!h->symbol_labels) {
		/*
		 * One block, branches resolved here: encode everything once into
		 * R->wb, noting the records that need a label offset (patched
		 * once the labels are known) or a fixup (applied in order after
		 * the words are in the frag).
		 */
		uint32_t at = 0, frag = as_fragment_current(R->assembler), *loff = R->label_word_offsets, *wb = R->word_buffer, npt = 0;
		codegen_commit_patch *pt = R->patches;
		ElfObj *o = R->object;
		uint64_t base;
		uint8_t *p;

		for (uint32_t k = 0; k < h->record_count; k++) {
			const a64_record *i = &in[k];
			unsigned n;

			if (i->opcode == A64_OP_LABEL) {
				loff[i->label] = at;
				continue;
			}
			n = a64_encode_record(i, wb + at, &pt[npt].fixup, NULL, 0);
			if (unlikely(i->opcode == A64_OP_MRS)) {
				int r = a64_sysreg(R->compile_context->strs[i->reference].s, strlen(R->compile_context->strs[i->reference].s), 0);

				if (r < 0)
					return codegen_commit_fail(R, "mrs: expected readable system register");
				wb[at] = 0xd5200000u | ((unsigned)r & 0xffff) << 5 | a64_encode_register_field(i->rd);
			}
			if (pt[npt].fixup.type) {
				pt[npt].record_index = k;
				pt[npt].word_position = at;
				pt[npt].is_relaxed = 0;
				npt++;
			}
			at += n;
		}
		if (unlikely(at > A64_BRANCH_IMMEDIATE19_MAX))	/* a label can be out of a conditional branch's reach */
			at += a64_commit_relax(in, h->record_count, wb, at, loff, pt, npt);
		elf_code(o, frag);
		base = elf_frag(o, frag)->size;
		p = elf_grow(o, frag, (size_t)at * 4);
		for (uint32_t j = 0; j < npt; j++) {
			const a64_record *i = &in[pt[j].record_index];
			uint32_t a = pt[j].word_position;
			int64_t d;

			if (!pt[j].fixup.label_flag)
				continue;
			if (pt[j].is_relaxed) {
				/* b.!cond / cbnz<->cbz over the next word (the record was encoded with offset 0) */
				if (i->opcode == A64_OP_BC && i->condition >= 14)
					return codegen_commit_fail(R, "internal: relaxing an unconditional b.al");
				wb[a] = (i->opcode == A64_OP_BC ? wb[a] ^ 1 : wb[a] ^ 1u << 24) | 2u << 5;
				wb[a + 1] = a64_b(((int64_t)loff[i->label] - (a + 1)) * 4);
				continue;
			}
			d = (int64_t)loff[i->label] - a;
			if (i->opcode == A64_OP_TBZ || i->opcode == A64_OP_TBNZ ? d < -(1 << 13) || d >= 1 << 13 :
			    a64_record_is_compare_branch(i->opcode) ? d < A64_BRANCH_IMMEDIATE19_MIN || d > A64_BRANCH_IMMEDIATE19_MAX :
			    i->opcode == A64_OP_ADR ? d < A64_BRANCH_IMMEDIATE19_MIN || d > A64_BRANCH_IMMEDIATE19_MAX :
			    i->opcode == A64_OP_B ? d < -(1 << 25) || d >= 1 << 25 : false)
				return codegen_commit_fail(R, i->opcode == A64_OP_ADR ? "function too large: adr to a label beyond 1 MB" :
							   "function too large: branch out of range");
			a64_encode_record(i, wb + a, &f, loff, a);
		}
		memcpy(p, wb, (size_t)at * 4);
		for (uint32_t j = 0; j < npt; j++) {
			const codegen_commit_fixup *g = &pt[j].fixup;
			uint32_t a = pt[j].word_position, y;

			if (g->label_flag)
				continue;
			y = codegen_commit_symbol_find(R, g->reference);
			elf_fixup(o, frag, base + 4u * (a + g->word_index), p + 4 * (a + g->word_index), g->type, as_symbol_value(R->assembler, y, g->offset));
			a64_commit_fixups_add(o, frag, base + 4u * a, p + 4 * a, &in[pt[j].record_index], y, R->assembler);
		}
		return 0;
	}
	/*
	 * Inline asm: in order.  Labels the asm text names (asm goto) are
	 * symbols; branches to the others are resolved here when label and
	 * branch end up in the same frag, else through the label's symbol.
	 */
	{
		ElfObj *o = R->object;
		uint32_t npend = 0;

		R->serial++;
		for (const char *q = raw, *qe = raw + h->raw_count; (q = memmem(q, (size_t)(qe - q), ".LBB", 4)); ) {
			uint32_t fid = 0, l = 0;
			const char *d = q += 4;

			for (int v; q < qe && (v = a64_hex_digit_value(*q)) >= 0; q++)
				fid = fid * 16 + (uint32_t)v;
			if (q == d || q >= qe || *q != '_' || fid != h->function_id)
				continue;
			d = ++q;
			for (int v; q < qe && (v = a64_hex_digit_value(*q)) >= 0; q++)
				l = l * 16 + (uint32_t)v;
			if (q > d && l < h->label_count) {
				codegen_commit_label *L = a64_commit_label_state(R, l);

				L->flags |= A64_COMMIT_LABEL_NAMED;
			}
		}
		for (uint32_t k = 0; k < h->record_count; k++) {
			const a64_record *i = &in[k];
			unsigned n;

			if (i->opcode == A64_OP_RAW) {
				if (as_snippet_assemble(R->assembler, raw + i->offset, i->reference))
					return -1;
				continue;
			}
			if (i->opcode == A64_OP_LABEL) {
				codegen_commit_label *L = a64_commit_label_state(R, i->label);
				uint32_t frag = as_fragment_current(R->assembler);

				if (L->flags & A64_COMMIT_LABEL_NAMED) {
					if (as_label_define_symbol(R->assembler, a64_commit_label(R, h->function_id, i->label)))
						return -1;
					continue;
				}
				L->flags |= A64_COMMIT_LABEL_PLACED;
				L->fragment = frag;
				L->offset = (uint32_t)elf_frag(o, frag)->size;
				for (uint32_t j = L->pending; j; j = R->pending[j - 1].next) {
					const codegen_commit_pending_branch *pd = &R->pending[j - 1];

					if (!a64_commit_branch(R, h->function_id, &in[pd->record_index], L, pd->fragment, pd->offset, pd->bytes))
						return -1;
				}
				continue;
			}
			if (i->opcode == A64_OP_ADR || i->opcode == A64_OP_B || i->opcode == A64_OP_BC || i->opcode == A64_OP_CBZ || i->opcode == A64_OP_CBNZ || i->opcode == A64_OP_TBZ ||
			    i->opcode == A64_OP_TBNZ) {
				codegen_commit_label *L = a64_commit_label_state(R, i->label);

				if (!(L->flags & A64_COMMIT_LABEL_NAMED)) {
					uint32_t frag = as_fragment_current(R->assembler), off;
					uint8_t *p;

					elf_code(o, frag);
					off = (uint32_t)elf_frag(o, frag)->size;
					p = elf_grow(o, frag, 4);
					if (L->flags & A64_COMMIT_LABEL_PLACED) {
						if (!a64_commit_branch(R, h->function_id, i, L, frag, off, p))
							return -1;
					} else {
						codegen_commit_pending_branch *pd = &R->pending[npend++];

						*pd = (codegen_commit_pending_branch){ L->pending, k, frag, off, p };
						L->pending = npend;
						a64_encode_record(i, w, &f, NULL, 0);
						memcpy(p, w, 4);
					}
					continue;
				}
			}
			n = a64_encode_record(i, w, &f, NULL, 0);
			if (unlikely(i->opcode == A64_OP_MRS)) {
				int r = a64_sysreg(R->compile_context->strs[i->reference].s, strlen(R->compile_context->strs[i->reference].s), 0);

				if (r < 0)
					return codegen_commit_fail(R, "mrs: expected readable system register");
				w[0] = 0xd5200000u | ((unsigned)r & 0xffff) << 5 | a64_encode_register_field(i->rd);
			}
			if (n)
				a64_commit_words_add(R, as_fragment_current(R->assembler), w, n, &f, h->function_id, i);
		}
		/* branches to labels never placed: through the (undefined) symbol, as the text would */
		for (uint32_t j = 0; j < npend; j++) {
			const codegen_commit_pending_branch *pd = &R->pending[j];
			const a64_record *i = &in[pd->record_index];

			if (!(a64_commit_label_state(R, i->label)->flags & A64_COMMIT_LABEL_PLACED)) {
				a64_encode_record(i, w, &f, NULL, 0);
				elf_fixup(o, pd->fragment, pd->offset, pd->bytes, f.type, as_symbol_value(R->assembler, a64_commit_label(R, h->function_id, i->label), 0));
			}
		}
	}
	return 0;
}
