/* vcc code generator, x86-64: two-address form and the AT&T printer (x64impl.h) */
#include "x64impl.h"

/* ---------------- two-address form ---------------- */

/* ops whose result must be their first operand on x86 */
static bool x64_tied(int op)
{
	return (op >= X_ADD && op <= X_SBB) || (op >= X_ADDI && op <= X_XORI) || (op >= X_SHLI && op <= X_SAR) ||
	       op == X_NEG || op == X_NOT || op == X_BSWAP || (op >= X_FADD && op <= X_FXOR);
}

static bool x64_commutes(int op)
{
	return op == X_ADD || op == X_AND || op == X_OR || op == X_XOR || op == X_IMUL || op == X_FADD || op == X_FMUL || op == X_FXOR;
}

/*
 * r0 = r1 op r2 as x86 has it, r0 = r0 op r2: a mov in front when r0 != r1;
 * a commutative op whose result is its second operand swaps instead, and
 * a non-commutative one takes the second operand through r11.
 */
VPRIV void x64_twoaddr(ecx *e)
{
	bool copies = e->E->o->opt >= 1 && !e->E->noopt;
	xst *X = e->x64;
	uint32_t n = 0;
	rec *o = X->tw;

	uint32_t j = 0;

	for (uint32_t k = 0; k < X->nin; k++) {
		rec i = X->in[k];

		if (e->ndl)
			dbg_remap(e, &j, k, n);	/* (-g rows: to the first record emitted for k) */
		if (copies && i.op == X_MOV && x64_copy_forward_at(e, k))
			continue;
		if (x64_tied(i.op) && i.r[0] != i.r[1]) {
			if (i.r[2] != XNOREG && i.r[0] == i.r[2]) {
				if (x64_commutes(i.op)) {
					i.r[2] = i.r[1];
				} else {
					int t = IS_XMM(i.r[2]) ? XMM15 : R11;

					o[n++] = (rec){ .op = X_MOV, .sz = 8, .r = { (uint8_t)t, i.r[2], XNOREG, XNOREG } };
					o[n++] = (rec){ .op = X_MOV, .sz = 8, .r = { i.r[0], i.r[1], XNOREG, XNOREG } };
					i.r[2] = (uint8_t)t;
				}
			} else {
				o[n++] = (rec){ .op = X_MOV, .sz = 8, .r = { i.r[0], i.r[1], XNOREG, XNOREG } };
			}
			i.r[1] = i.r[0];
		}
		o[n++] = i;
	}
	if (e->ndl)
		dbg_remap(e, &j, UINT32_MAX, n);
	X->tw = X->in;
	X->in = o;
	X->nin = n;
}

/* ---------------- printing ---------------- */

static const char *const x64_r64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
				       "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip" };
static const char *const x64_r32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
				       "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" };
static const char *const x64_r16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
				       "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" };
static const char *const x64_r8[] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
				      "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" };
static const char *const x64_ccn[] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };

VPRIV int x64_asm_namedreg(const char *name)
{
	if (*name == '%') name++;
	for (int r = 0; r < 16; r++)
		if (!strcmp(name, x64_r64[r]) || !strcmp(name, x64_r32[r]) ||
		    !strcmp(name, x64_r16[r]) || !strcmp(name, x64_r8[r])) return r;
	return -1;
}

VPRIV void x64_reg(tbuf *b, int r, int sz)
{
	if (IS_XMM(r)) {
		tb_s(b, "%xmm");
		tb_u(b, (uint64_t)(r - XMM0));
		return;
	}
	tb_c(b, '%');
	tb_s(b, sz == 8 ? x64_r64[r] : sz == 4 ? x64_r32[r] : sz == 2 ? x64_r16[r] : x64_r8[r]);
}

static char x64_sfx(int sz) { return sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b'; }

VPRIV void x64_lbl(tbuf *b, uint32_t s, uint32_t l)
{
	tb_s(b, ".LBB");
	tb_u(b, s);
	tb_c(b, '_');
	tb_u(b, l);
}

static void x64_memop(ecx *e, tbuf *b, const rec *i)
{
	cc *c = e->c;
	int base = i->r[1];

	/* Incoming stack arguments retain rbp; local slots use the realigned base. */
	if (base == RBP && i->off < 0 && e->wk->falign > 16)
		base = R12;
	if (i->r[1] == RIP) {
		ref_off(c, b, i->ref, i->off);
		tb_s(b, "(%rip)");
		return;
	}
	if (i->off)
		tb_i(b, i->off);
	tb_c(b, '(');
	x64_reg(b, base, 8);
	tb_c(b, ')');
}

/* "\top" with the size suffix, then a tab */
static void x64_ins(tbuf *b, const char *op, int sz)
{
	tb_c(b, '\t');
	tb_s(b, op);
	if (sz)
		tb_c(b, x64_sfx(sz));
	tb_c(b, '\t');
}

static void x64_imm(tbuf *b, int64_t v)
{
	tb_c(b, '$');
	tb_i(b, v);
}

/* Prologue callee-save slots can follow a frame larger than disp32. */
static void x64_cs_access(ecx *e, tbuf *b, int r, bool load)
{
	int64_t off = e->x64->csslot[r];
	int base = e->wk->falign > 16 ? R12 : RBP;
	if (off < INT32_MIN || off > INT32_MAX) {
		tb_s(b, "\tmovabsq\t$"); tb_i(b, off);
		tb_s(b, ", %r11\n\taddq\t"); x64_reg(b, base, 8);
		tb_s(b, ", %r11\n");
		base = R11, off = 0;
	}
	tb_s(b, "\tmovq\t");
	if (!load) { x64_reg(b, r, 8); tb_s(b, ", "); }
	if (off) tb_i(b, off);
	tb_c(b, '('); x64_reg(b, base, 8); tb_c(b, ')');
	if (load) { tb_s(b, ", "); x64_reg(b, r, 8); }
	tb_c(b, '\n');
}

/* CFA stays rbp+16, but a realigned save slot is addressed through r12. */
static void x64_cs_cfi(tbuf *b, int r, int64_t off)
{
	uint8_t leb[10];
	unsigned n = 0;
	for (;;) {
		unsigned byte = (unsigned)off & 127;
		off >>= 7;
		bool last = (off == 0 && !(byte & 64)) || (off == -1 && (byte & 64));
		leb[n++] = (uint8_t)(byte | (last ? 0 : 128));
		if (last) break;
	}
	/* DW_CFA_expression, register (rbx/r12-r15), length, DW_OP_breg12. */
	tb_f(b, "\t.cfi_escape 0x10, %u, %u, 0x7c", (unsigned)r, n + 1);
	for (unsigned i = 0; i < n; i++) tb_f(b, ", %u", (unsigned)leb[i]);
	tb_c(b, '\n');
}

VPRIV void x64_print(ecx *e, tbuf *b, uint32_t s)
{
	cc *c = e->c;
	xst *X = e->x64;
	static const char *const alu[] = { [X_ADD] = "add", [X_SUB] = "sub", [X_AND] = "and", [X_OR] = "or", [X_XOR] = "xor",
					   [X_IMUL] = "imul", [X_ADC] = "adc", [X_SBB] = "sbb", [X_ADDI] = "add", [X_SUBI] = "sub", [X_ANDI] = "and", [X_ORI] = "or",
					   [X_XORI] = "xor", [X_SHLI] = "shl", [X_SHRI] = "shr", [X_SARI] = "sar", [X_SHL] = "shl",
					   [X_SHR] = "shr", [X_SAR] = "sar", [X_NEG] = "neg", [X_NOT] = "not" };
	int64_t frame = (e->wk->foff + 15) & ~(int64_t)15;
	uint32_t A = e->wk->falign;
	bool cfi = e->E->o->cfi;	/* unwind tables: .cfi_* as clang writes them */
	uint32_t dj = 0;		/* -g: the next row */

	if (unlikely(e->E->o->debug)) {
		/* the function's own line (its body's '{', column 0), as clang */
		uint32_t f, l, cl;

		if (dbg_where(e, c->syms[s].body ? N(c->syms[s].body)->loc : c->syms[s].loc, &f, &l, &cl))
			tb_f(b, "\t.loc\t%u %u 0\n", f, l);
	}
	tb_s(b, cfi ? "\t.cfi_startproc\n\tpushq\t%rbp\n\t.cfi_def_cfa_offset 16\n\t.cfi_offset %rbp, -16\n"
		      "\tmovq\t%rsp, %rbp\n\t.cfi_def_cfa_register %rbp\n" : "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n");
	if (A > 16) {
		tb_s(b, "\tpushq\t%r12\n");
		if (cfi) tb_s(b, "\t.cfi_offset %r12, -24\n");
		tb_s(b, "\tandq\t$");
		tb_i(b, -(int64_t)A);
		tb_s(b, ", %rsp\n\tmovq\t%rsp, %r12\n");
	}
	if (frame > INT32_MAX) {
		tb_s(b, "\tmovabsq\t$");
		tb_i(b, frame);
		tb_s(b, ", %r11\n\tsubq\t%r11, %rsp\n");
	} else if (frame) {
		tb_s(b, "\tsubq\t$");
		tb_i(b, frame);
		tb_s(b, ", %rsp\n");
	}
	for (int r = 0; r < 16; r++)
		if (X->csslot[r]) {
			x64_cs_access(e, b, r, false);
			if (cfi) {
				if (A > 16) x64_cs_cfi(b, r, X->csslot[r]);
				else tb_f(b, "\t.cfi_offset %%%s, %lld\n", x64_r64[r], (long long)(X->csslot[r] - 16));
			}
		}
	for (uint32_t k = 0; k < X->nin; k++) {
		const rec *i = &X->in[k];
		int sz = i->sz;

		for (; dj < e->ndl && e->dl[dj].k == k; dj++)
			if (dj + 1 == e->ndl || e->dl[dj + 1].k != k)	/* the last of several at one place */
				tb_f(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
				     e->dl[dj].file, e->dl[dj].line, e->dl[dj].col);

		switch (i->op) {
		case X_LABEL:
			x64_lbl(b, s, i->label);
			tb_s(b, ":\n");
			continue;
		case X_MOV:
			if (IS_XMM(i->r[0]) && IS_XMM(i->r[1]))
				tb_s(b, "\tmovaps\t");
			else if (IS_XMM(i->r[0]) || IS_XMM(i->r[1]))
				tb_s(b, sz == 4 ? "\tmovd\t" : "\tmovq\t");
			else
				x64_ins(b, "mov", sz);
			x64_reg(b, i->r[1], sz);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_MOVI: {
			int64_t v = i->imm;

			if (sz == 4) {
				x64_ins(b, "mov", 4);
				x64_imm(b, (int32_t)v);
				tb_s(b, ", ");
				x64_reg(b, i->r[0], 4);
			} else if (v >= INT32_MIN && v <= INT32_MAX) {
				x64_ins(b, "mov", 8);
				x64_imm(b, v);
				tb_s(b, ", ");
				x64_reg(b, i->r[0], 8);
			} else if ((uint64_t)v <= UINT32_MAX) {
				x64_ins(b, "mov", 4);
				x64_imm(b, v);
				tb_s(b, ", ");
				x64_reg(b, i->r[0], 4);
			} else {
				x64_ins(b, "movabs", 8);
				x64_imm(b, v);
				tb_s(b, ", ");
				x64_reg(b, i->r[0], 8);
			}
			break;
		}
		case X_LD:
			if (IS_XMM(i->r[0])) {
				tb_s(b, sz == 4 ? "\tmovss\t" : sz == 8 ? "\tmovsd\t" : "\tmovups\t");
				x64_memop(e, b, i);
				tb_s(b, ", ");
				x64_reg(b, i->r[0], 8);
				break;
			}
			{
			static const char *const sx[] = { [1] = "\tmovsbq\t", [2] = "\tmovswq\t", [4] = "\tmovslq\t", [8] = "\tmovq\t" };
			static const char *const zx[] = { [1] = "\tmovzbl\t", [2] = "\tmovzwl\t", [4] = "\tmovl\t", [8] = "\tmovq\t" };

			tb_s(b, i->cond ? sx[sz] : zx[sz]);
			x64_memop(e, b, i);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], i->cond || sz == 8 ? 8 : 4);
			break;
			}
		case X_ST:
			if (IS_XMM(i->r[2]))
				tb_s(b, sz == 4 ? "\tmovss\t" : sz == 8 ? "\tmovsd\t" : "\tmovups\t");
			else
				x64_ins(b, "mov", sz);
			x64_reg(b, i->r[2], sz);
			tb_s(b, ", ");
			x64_memop(e, b, i);
			break;
		case X_LEA:
			x64_ins(b, "lea", 8);
			x64_memop(e, b, i);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_ADD: case X_SUB: case X_AND: case X_OR: case X_XOR: case X_IMUL: case X_ADC: case X_SBB:
			x64_ins(b, alu[i->op], sz);
			x64_reg(b, i->r[2], sz);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_ADDI: case X_SUBI: case X_ANDI: case X_ORI: case X_XORI: case X_SHLI: case X_SHRI: case X_SARI:
			x64_ins(b, alu[i->op], sz);
			x64_imm(b, i->imm);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_IMULI:
			x64_ins(b, "imul", sz);
			x64_imm(b, i->imm);
			tb_s(b, ", ");
			x64_reg(b, i->r[1], sz);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_SHL: case X_SHR: case X_SAR:
			x64_ins(b, alu[i->op], sz);
			tb_s(b, "%cl, ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_NEG: case X_NOT:
			x64_ins(b, alu[i->op], sz);
			x64_reg(b, i->r[0], sz);
			break;
		case X_CMP:
			x64_ins(b, "cmp", sz);
			x64_reg(b, i->r[2], sz);
			tb_s(b, ", ");
			x64_reg(b, i->r[1], sz);
			break;
		case X_CMPI:
			x64_ins(b, "cmp", sz);
			x64_imm(b, i->imm);
			tb_s(b, ", ");
			x64_reg(b, i->r[1], sz);
			break;
		case X_TEST:
			x64_ins(b, "test", sz);
			x64_reg(b, i->r[1], sz);
			tb_s(b, ", ");
			x64_reg(b, i->r[1], sz);
			break;
		case X_SETCC:
			tb_s(b, "\tset");
			tb_s(b, x64_ccn[i->cond]);
			tb_c(b, '\t');
			x64_reg(b, i->r[0], 1);
			tb_s(b, "\n\tmovzbl\t");
			x64_reg(b, i->r[0], 1);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 4);
			break;
		case X_JMP:
			tb_s(b, "\tjmp\t");
			x64_lbl(b, s, i->label);
			break;
		case X_JCC:
			tb_s(b, "\tj");
			tb_s(b, x64_ccn[i->cond]);
			tb_c(b, '\t');
			x64_lbl(b, s, i->label);
			break;
		case X_CALL:
			tb_s(b, "\tcallq\t");
			ref_name(c, b, i->ref);
			if (c->o->pic && (RKIND(i->ref) == RK_NAME || sym_needs_got(c, i->ref)))
				tb_s(b, "@PLT");
			break;
		case X_CALLR:
			tb_s(b, "\tcallq\t*");
			x64_reg(b, i->r[1], 8);
			break;
		case X_PUSH:
			tb_s(b, "\tpushq\t");
			x64_reg(b, i->r[1], 8);
			break;
		case X_POP:
			tb_s(b, "\tpopq\t");
			x64_reg(b, i->r[0], 8);
			break;
		case X_ADJSP:
			tb_s(b, i->imm < 0 ? "\tsubq\t$" : "\taddq\t$");
			tb_i(b, i->imm < 0 ? -i->imm : i->imm);
			tb_s(b, ", %rsp");
			break;
		case X_CQO:
			tb_s(b, sz == 8 ? "\tcqto" : "\tcltd");
			break;
		case X_XCHG: case X_XADD:
			x64_ins(b, i->op == X_XCHG ? "xchg" : "lock xadd", sz);
			x64_reg(b, i->r[0], sz);
			tb_s(b, ", ");
			x64_memop(e, b, i);
			break;
		case X_CMPXCHG:
			x64_ins(b, "lock cmpxchg", sz);
			x64_reg(b, i->r[2], sz);
			tb_s(b, ", ");
			x64_memop(e, b, i);
			break;
		case X_LEAL:
			tb_s(b, "\tleaq\t");
			x64_lbl(b, s, i->label);
			tb_s(b, "(%rip), ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_RAW:
			if (i->cond && e->wk->falign > 16)
				efatal(e, e->curloc, "inline asm r12 operand or clobber conflicts with aligned frame base");
			tb_s(b, "#APP\n");
			tb_put(b, e->raw.p + i->off, i->ref);
			tb_s(b, "\n#NO_APP");
			break;
		case X_JMPR:
			tb_s(b, "\tjmpq\t*");
			x64_reg(b, i->r[1], 8);
			break;
		case X_NAMED:
			ref_name(c, b, i->ref);
			tb_c(b, ':');
			break;
		case X_MFENCE:
			tb_s(b, "\tmfence");
			break;
		case X_RESUME_FRAME:
			/* Match the final prologue even if alignment was discovered after
			   setjmp (or inside an inline body). Bypass memory operand rewriting. */
			if (e->wk->falign > 16) {
				tb_s(b, "\tleaq\t-8(%rbp), %r12\n\tandq\t$");
				tb_i(b, -(int64_t)e->wk->falign);
				tb_s(b, ", %r12");
			}
			break;
		case X_BARRIER:
			tb_s(b, "\t# compiler barrier");
			break;
		case X_GOT:
			tb_s(b, "\tmovq\t");
			ref_off(c, b, i->ref, 0);
			tb_s(b, "@GOTPCREL(%rip), ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_TLS:
			if (i->cond == 2) {
				tb_s(b, "\tdata16\n\tleaq\t");
				ref_off(c, b, i->ref, 0);
				tb_s(b, "@TLSGD(%rip), %rdi\n\tdata16\n\tdata16\n\trex64\n\tcallq\t__tls_get_addr@PLT");
				break;
			}
			tb_s(b, "\tmovq\t%fs:0, ");
			x64_reg(b, i->r[0], 8);
			tb_s(b, i->cond ? "\n\taddq\t" : "\n\tleaq\t");
			ref_off(c, b, i->ref, 0);
			tb_s(b, i->cond ? "@GOTTPOFF(%rip), " : "@TPOFF(");
			if (!i->cond) {
				x64_reg(b, i->r[0], 8);
				tb_s(b, "), ");
			}
			x64_reg(b, i->r[0], 8);
			break;
		case X_UD2:
			tb_s(b, "\tud2");
			break;
		case X_BSF: case X_BSR:
			x64_ins(b, i->op == X_BSF ? "bsf" : "bsr", sz);
			x64_reg(b, i->r[1], sz);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_BSWAP:
			x64_ins(b, "bswap", sz);
			x64_reg(b, i->r[0], sz);
			break;
		case X_MULW:
			x64_ins(b, "mul", sz);
			x64_reg(b, i->r[1], sz);
			break;
		case X_IDIV: case X_DIV:
			x64_ins(b, i->op == X_IDIV ? "idiv" : "div", sz);
			x64_reg(b, i->r[1], sz);
			break;
		case X_EXT: {
			static const char *const sx[] = { [1] = "\tmovsbq\t", [2] = "\tmovswq\t", [4] = "\tmovslq\t" };
			static const char *const zx[] = { [1] = "\tmovzbl\t", [2] = "\tmovzwl\t", [4] = "\tmovl\t" };

			tb_s(b, i->cond ? sx[sz] : zx[sz]);
			x64_reg(b, i->r[1], sz);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], i->cond ? 8 : 4);
			break;
		}
		case X_FADD: case X_FSUB: case X_FMUL: case X_FDIV: {
			static const char *const fo[] = { "add", "sub", "mul", "div" };

			tb_c(b, '\t');
			tb_s(b, fo[i->op - X_FADD]);
			tb_s(b, sz == 4 ? "ss\t" : "sd\t");
			x64_reg(b, i->r[2], 8);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		}
		case X_FXOR:
			tb_s(b, "\txorps\t");
			x64_reg(b, i->r[2], 8);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_FSQRT:
			tb_s(b, "\tsqrtsd\t");
			x64_reg(b, i->r[1], 8);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_FCMP:
			tb_s(b, sz == 4 ? "\tucomiss\t" : "\tucomisd\t");
			x64_reg(b, i->r[2], 8);
			tb_s(b, ", ");
			x64_reg(b, i->r[1], 8);
			break;
		case X_FSETCC:
			tb_s(b, i->cond ? "\tsetne\t" : "\tsete\t");
			x64_reg(b, i->r[0], 1);
			tb_s(b, i->cond ? "\n\tsetp\t%r11b\n\torb\t%r11b, " : "\n\tsetnp\t%r11b\n\tandb\t%r11b, ");
			x64_reg(b, i->r[0], 1);
			tb_s(b, "\n\tmovzbl\t");
			x64_reg(b, i->r[0], 1);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 4);
			break;
		case X_CVTI2F:
			tb_s(b, sz == 4 ? "\tcvtsi2ss" : "\tcvtsi2sd");
			tb_c(b, i->sh == 8 ? 'q' : 'l');
			tb_c(b, '\t');
			x64_reg(b, i->r[1], i->sh);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_CVTF2I:
			tb_s(b, i->sh == 4 ? "\tcvttss2si" : "\tcvttsd2si");
			tb_c(b, sz == 8 ? 'q' : 'l');
			tb_c(b, '\t');
			x64_reg(b, i->r[1], 8);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_CVTF2F:
			tb_s(b, sz == 8 ? "\tcvtss2sd\t" : "\tcvtsd2ss\t");
			x64_reg(b, i->r[1], 8);
			tb_s(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_X87: {
			static const char *const xn[] = { [XF_LDT] = "fldt", [XF_STPT] = "fstpt", [XF_ADD] = "faddp\t%st, %st(1)",
				[XF_SUB] = "fsubrp\t%st, %st(1)", [XF_MUL] = "fmulp\t%st, %st(1)", [XF_DIV] = "fdivrp\t%st, %st(1)",
				[XF_CHS] = "fchs", [XF_UCOMIP] = "fucomip\t%st(1), %st", [XF_POP] = "fstp\t%st(0)",
				[XF_STCW] = "fnstcw", [XF_LDCW] = "fldcw", [XF_LDZ] = "fldz" };

			tb_c(b, '\t');
			if (i->cond == XF_LD || i->cond == XF_STP)
				tb_s(b, i->cond == XF_LD ? (sz == 4 ? "flds" : "fldl") : (sz == 4 ? "fstps" : "fstpl"));
			else if (i->cond == XF_ILD || i->cond == XF_ISTP)
				tb_s(b, i->cond == XF_ILD ? (sz == 2 ? "filds" : sz == 4 ? "fildl" : "fildll")
							  : (sz == 2 ? "fistps" : sz == 4 ? "fistpl" : "fistpll"));
			else
				tb_s(b, xn[i->cond]);
			if (i->r[1] != XNOREG) {
				tb_c(b, '\t');
				x64_memop(e, b, i);
			}
			break;
		}
		case X_REP:
			tb_s(b, i->cond ? "\trep\tstosb" : "\trep\tmovsb");
			break;
		default:
			efatal(e, e->curloc, "internal error: x86-64 record %d", i->op);
		}
		tb_c(b, '\n');
	}
	for (int r = 0; r < 16; r++)
		if (X->csslot[r]) {
			x64_cs_access(e, b, r, true);
			if (cfi) tb_f(b, "\t.cfi_same_value %%%s\n", x64_r64[r]);
		}
	for (; dj < e->ndl; dj++)
		if (dj + 1 == e->ndl)
			tb_f(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
			     e->dl[dj].file, e->dl[dj].line, e->dl[dj].col);
	if (e->wk->falign > 16) {
		tb_s(b, "\tmovq\t-8(%rbp), %r12\n");
		if (cfi) tb_s(b, "\t.cfi_same_value %r12\n");
	}
	tb_s(b, cfi ? "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n\t.cfi_def_cfa %rsp, 8\n\tretq\n\t.cfi_endproc\n.Lfunc_end" :
		      "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n\tretq\n.Lfunc_end");
	tb_u(b, s);
	tb_s(b, ":\n\t.size\t");
	sym_name(c, b, s);
	tb_s(b, ", .Lfunc_end");
	tb_u(b, s);
	tb_c(b, '-');
	sym_name(c, b, s);
	tb_c(b, '\n');
}
