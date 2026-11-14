/* vcc code generator, x86-64: two-address form and the AT&T printer (x64-internal.h) */
#include "x64-internal.h"

/* ---------------- two-address form ---------------- */

/* Operand constraints indexed by the record's byte-sized opcode. */
enum { X64_TIED = 1, X64_TIED_COMMUTATIVE };
static const uint8_t x64_two_address_kind[256] = {
	[X64_OP_ADD] = X64_TIED_COMMUTATIVE, [X64_OP_SUB] = X64_TIED,
	[X64_OP_AND] = X64_TIED_COMMUTATIVE, [X64_OP_OR] = X64_TIED_COMMUTATIVE,
	[X64_OP_XOR] = X64_TIED_COMMUTATIVE, [X64_OP_IMUL] = X64_TIED_COMMUTATIVE,
	[X64_OP_ADC] = X64_TIED, [X64_OP_SBB] = X64_TIED,
	[X64_OP_ADDI] = X64_TIED, [X64_OP_SUBI] = X64_TIED, [X64_OP_ANDI] = X64_TIED,
	[X64_OP_ORI] = X64_TIED, [X64_OP_XORI] = X64_TIED,
	[X64_OP_SHLI] = X64_TIED, [X64_OP_SHRI] = X64_TIED, [X64_OP_SARI] = X64_TIED,
	[X64_OP_SHL] = X64_TIED, [X64_OP_SHR] = X64_TIED, [X64_OP_SAR] = X64_TIED,
	[X64_OP_NEG] = X64_TIED, [X64_OP_NOT] = X64_TIED, [X64_OP_BSWAP] = X64_TIED,
	[X64_OP_CRC32Q] = X64_TIED, [X64_OP_CMOV] = X64_TIED,
	[X64_OP_FADD] = X64_TIED_COMMUTATIVE, [X64_OP_FSUB] = X64_TIED,
	[X64_OP_FMUL] = X64_TIED_COMMUTATIVE, [X64_OP_FDIV] = X64_TIED,
	[X64_OP_FXOR] = X64_TIED_COMMUTATIVE
};

/*
 * r0 = r1 op r2 as x86 has it, r0 = r0 op r2: a mov in front when r0 != r1;
 * a commutative op whose result is its second operand swaps instead, and
 * a non-commutative one takes the second operand through r11.
 */
VPRIV void x64_two_address_convert(codegen_context *e)
{
	bool copies = e->unit->options->opt >= 1 && !e->unit->cleanups_disabled;
	x64_lowering_context *X = e->x64_context;
	uint32_t n = 0;
	record *o = X->two_address_records;

	uint32_t j = 0;

	X->frame_free = x64_framefree_candidate(e);
	for (uint32_t k = 0; k < X->record_count; k++) {
		const record *i = &X->records[k];

		if (e->debug_row_count)
			codegen_debug_rows_remap(e, &j, k, n);	/* (-g rows: to the first record emitted for k) */
		if (copies && i->opcode == X64_OP_MOV && x64_copy_forward_at(e, k))
			continue;
		if (X->frame_free && !x64_framefree_record(e, i))
			X->frame_free = false;
		unsigned kind = x64_two_address_kind[i->opcode];
		if (!kind || i->registers[0] == i->registers[1]) {
			o[n++] = *i;
			continue;
		}
		uint8_t rhs = i->registers[2];
		if (i->registers[2] != X64_REGISTER_NONE && i->registers[0] == i->registers[2]) {
			if (kind == X64_TIED_COMMUTATIVE) {
				rhs = i->registers[1];
			} else {
				int t = X64_REGISTER_IS_XMM(i->registers[2]) ? XMM15 : R11;

				o[n++] = (record){ .opcode = X64_OP_MOV, .size = 8, .registers = { (uint8_t)t, i->registers[2], X64_REGISTER_NONE, X64_REGISTER_NONE } };
				o[n++] = (record){ .opcode = X64_OP_MOV, .size = 8, .registers = { i->registers[0], i->registers[1], X64_REGISTER_NONE, X64_REGISTER_NONE } };
				rhs = (uint8_t)t;
			}
		} else {
			o[n++] = (record){ .opcode = X64_OP_MOV, .size = 8, .registers = { i->registers[0], i->registers[1], X64_REGISTER_NONE, X64_REGISTER_NONE } };
		}
		o[n] = *i;
		o[n].registers[1] = i->registers[0];
		o[n++].registers[2] = rhs;
	}
	if (e->debug_row_count)
		codegen_debug_rows_remap(e, &j, UINT32_MAX, n);
	X->two_address_records = X->records;
	X->records = o;
	X->record_count = n;
}

/* ---------------- printing ---------------- */

static const char *const x64_register_names_64[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
				       "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip" };
static const char *const x64_register_names_32[] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
				       "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d" };
static const char *const x64_register_names_16[] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
				       "r8w", "r9w", "r10w", "r11w", "r12w", "r13w", "r14w", "r15w" };
static const char *const x64_register_names_8[] = { "al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil",
				      "r8b", "r9b", "r10b", "r11b", "r12b", "r13b", "r14b", "r15b" };
static const char *const x64_condition_names[] = { "o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g" };

VPRIV void x64_register_name_append(tbuf *b, int r, int sz)
{
	if (X64_REGISTER_IS_XMM(r)) {
		vtext_string_append(b, "%xmm");
		vtext_unsigned_append(b, (uint64_t)(r - XMM0));
		return;
	}
	vtext_char_append(b, '%');
	vtext_string_append(b, sz == 8 ? x64_register_names_64[r] : sz == 4 ? x64_register_names_32[r] : sz == 2 ? x64_register_names_16[r] : x64_register_names_8[r]);
}

static char x64_size_suffix(int sz) { return sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b'; }

VPRIV void x64_label_name_append(tbuf *b, uint32_t s, uint32_t l)
{
	vtext_append(b, ".LBB", sizeof ".LBB" - 1);
	vtext_unsigned_append(b, s);
	vtext_char_append(b, '_');
	vtext_unsigned_append(b, l);
}

static void x64_memory_operand_append(codegen_context *e, tbuf *b, const record *i)
{
	cc *c = e->compile_context;
	int base = i->registers[1];
	int64_t off=i->offset;
	if(base==X64_REGISTER_FRAME) {
		base=RSP;off+=e->x64_context->frame_size-8-e->x64_context->stack_pointer_delta;
	}

	/* Incoming stack arguments retain rbp; local slots use the realigned base. */
	if (base == RBP && i->offset < 0 && e->walk_state->frame_alignment > 16)
		base = R12;
	else if (base == RBP && i->offset < 0)
		off -= e->x64_context->wsave;	/* (the Windows frame: below the save area) */
	if (i->registers[1] == RIP) {
		codegen_reference_offset_append(c, b, i->reference, i->offset);
		vtext_string_append(b, "(%rip)");
		return;
	}
	if (off)
		vtext_signed_append(b, off);
	vtext_char_append(b, '(');
	x64_register_name_append(b, base, 8);
	if (i->opcode == X64_OP_LDX || i->opcode == X64_OP_STX || ((i->opcode == X64_OP_TESTM || i->opcode == X64_OP_CMPM) && i->registers[3] != X64_REGISTER_NONE)) {
		vtext_char_append(b, ','); x64_register_name_append(b, i->registers[3], 8);
		vtext_char_append(b, ','); vtext_unsigned_append(b, 1u << i->shift);
	}
	vtext_char_append(b, ')');
}

/* "\top" with the size suffix, then a tab */
static void x64_mnemonic_append(tbuf *b, const char *op, int sz)
{
	vtext_char_append(b, '\t');
	vtext_string_append(b, op);
	if (sz)
		vtext_char_append(b, x64_size_suffix(sz));
	vtext_char_append(b, '\t');
}

static void x64_immediate_append(tbuf *b, int64_t v)
{
	vtext_char_append(b, '$');
	vtext_signed_append(b, v);
}

/* the stack protector's guard into r11: %fs:40 (glibc's TCB), or as -mstack-protector-guard-* say */
static void x64_stack_guard_append(codegen_context *e, tbuf *b)
{
	const cc_options *o = e->unit->options;

	if (CODEGEN_IS_WIN64(e->compile_context) && !o->sp_sym && !o->sp_gs) {
		/* MinGW: libssp's variable, extern data through its .refptr. pointer as clang */
		vtext_string_append(b, "\tmovq\t.refptr.__stack_chk_guard(%rip), %r11\n\tmovq\t(%r11), %r11\n");
		atomic_store_explicit(&e->unit->stack_guard_refptr, 1, memory_order_relaxed);
	}
	else if (o->sp_sym)
		vtext_format_append(b, "\tmovq\t%%%s:%s(%%rip), %%r11\n", o->sp_gs ? "gs" : "fs", o->sp_sym);
	else
		vtext_format_append(b, "\tmovq\t%%%s:40, %%r11\n", o->sp_gs ? "gs" : "fs");
}

/* Prologue callee-save slots can follow a frame larger than disp32. */
static void x64_callee_save_access_append(codegen_context *e, tbuf *b, int r, bool load)
{
	int64_t off = e->x64_context->callee_save_slots[r];
	int base = e->walk_state->frame_alignment > 16 ? R12 : RBP;
	if(e->x64_context->stack_pointer_frame) {base=RSP;off+=e->x64_context->frame_size-8;}
	if (off < INT32_MIN || off > INT32_MAX) {
		vtext_string_append(b, "\tmovabsq\t$"); vtext_signed_append(b, off);
		vtext_string_append(b, ", %r11\n\taddq\t"); x64_register_name_append(b, base, 8);
		vtext_string_append(b, ", %r11\n");
		base = R11, off = 0;
	}
	vtext_string_append(b, "\tmovq\t");
	if (!load) { x64_register_name_append(b, r, 8); vtext_string_append(b, ", "); }
	if (off) vtext_signed_append(b, off);
	vtext_char_append(b, '('); x64_register_name_append(b, base, 8); vtext_char_append(b, ')');
	if (load) { vtext_string_append(b, ", "); x64_register_name_append(b, r, 8); }
	vtext_char_append(b, '\n');
}

/* CFA stays rbp+16, but a realigned save slot is addressed through r12. */
static void x64_callee_save_cfi_append(tbuf *b, int r, int64_t off)
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
	vtext_format_append(b, "\t.cfi_escape 0x10, %u, %u, 0x7c", (unsigned)r, n + 1);
	for (unsigned i = 0; i < n; i++) vtext_format_append(b, ", %u", (unsigned)leb[i]);
	vtext_char_append(b, '\n');
}

/* ---------------- the Windows frame (PLAN-win64.md WIN3) ---------------- */

#define XW_BIT(r) (UINT64_C(1) << (r))
/* callee-saved: Microsoft's rbx rsi rdi r12-r15 xmm6-xmm15; System V's rbx r12-r15 (rbp: the frame) */
#define XW_SYSV_CS (XW_BIT(RBX) | XW_BIT(R12) | XW_BIT(R13) | XW_BIT(R14) | XW_BIT(R15))
#define XW_MS_CS (XW_SYSV_CS | XW_BIT(RSI) | XW_BIT(RDI) | (UINT64_C(0x3ff) << (XMM0 + 6)))

/* the registers an asm text names (%rsi, %esi, %xmm7, %ymm7 ...): it may write them */
static uint64_t x64w_textregs(const char *p, size_t n)
{
	uint64_t m = 0;

	for (size_t k = 0; k + 1 < n; k++) {
		char nm[8];
		size_t j = 0;
		int r;

		if (p[k] != '%')
			continue;
		while (j < sizeof nm - 1 && k + 1 + j < n && ((p[k + 1 + j] >= 'a' && p[k + 1 + j] <= 'z') || (p[k + 1 + j] >= '0' && p[k + 1 + j] <= '9')))
			nm[j] = p[k + 1 + j], j++;
		nm[j] = 0;
		if ((nm[0] == 'y' || nm[0] == 'z') && nm[1] == 'm' && nm[2] == 'm')
			nm[0] = 'x';	/* (a ymm/zmm write changes its xmm) */
		r = x64_asm_register_find(nm);
		if (r >= 0)
			m |= XW_BIT(r);
	}
	return m;
}

/* the callee-saved registers (of the function's convention) the body writes: the Windows
   prologue saves them; a call writes what its callee's convention does not preserve */
static uint64_t x64w_saves(codegen_context *e)
{
	x64_lowering_context *X = e->x64_context;
	uint64_t def = X->csmask;

	for (uint32_t k = 0; k < X->record_count; k++) {
		const record *i = &X->records[k];
		record_effects f;

		if (i->opcode == X64_OP_RAW) {
			def |= x64w_textregs(e->raw.p + i->offset, i->reference);
			continue;
		}
		f = x64_record_effects(e, i);
		if ((f.flags & RECORD_EFFECT_CALL) && (i->opcode == X64_OP_CALL || i->opcode == X64_OP_CALLR) && i->shift)
			continue;	/* a Microsoft callee keeps them all */
		def |= f.defines;
	}
	if (e->walk_state->frame_alignment > 16)
		def |= XW_BIT(R12);	/* the realigned frame base */
	return def & (X->msfn ? XW_MS_CS : XW_SYSV_CS);
}

/* the save area's size: the XMM registers first (16-aligned), then the GPRs */
static int64_t x64w_area(uint64_t saves)
{
	int nx = __builtin_popcountll(saves >> XMM0), ng = __builtin_popcountll(saves & 0xffff);

	return (16 * nx + 8 * ng + 15) & ~(int64_t)15;
}

/*
 * The prologue in the shape the Windows unwinder decodes: push rbp; one allocation for the save
 * area; the saves (mov, movaps); rbp = the entry's frame base (rsp + the area: .seh_setframe's
 * offset is at most 240, which the area never exceeds); .seh_endprologue.  The locals and any
 * realignment come after, in the body, where rbp is the frame register the unwinder reads.
 * Without seh (an ms_abi function on another OS) the same code without the directives.
 */
static void x64w_prologue(codegen_context *e, tbuf *b, uint32_t s, uint64_t saves, int64_t frame, uint32_t A)
{
	cc *c = e->compile_context;
	bool win = CODEGEN_IS_WIN64(c), seh = win && !c->o->no_seh;	/* (seh: the unwind directives) */
	int64_t S = e->x64_context->wsave, off = 0;

	(void)s;
	vtext_string_append(b, seh ? "\tpushq\t%rbp\n\t.seh_pushreg %rbp\n" : "\tpushq\t%rbp\n");
	if (S) {
		vtext_format_append(b, "\tsubq\t$%lld, %%rsp\n", (long long)S);
		if (seh) vtext_format_append(b, "\t.seh_stackalloc %lld\n", (long long)S);
		for (int r = XMM0; r <= XMM15; r++)
			if (saves & XW_BIT(r)) {
				vtext_format_append(b, "\tmovaps\t%%xmm%d, %lld(%%rsp)\n", r - XMM0, (long long)off);
				if (seh) vtext_format_append(b, "\t.seh_savexmm %%xmm%d, %lld\n", r - XMM0, (long long)off);
				off += 16;
			}
		for (int r = 0; r < 16; r++)
			if (saves & XW_BIT(r)) {
				vtext_format_append(b, "\tmovq\t%%%s, %lld(%%rsp)\n", x64_register_names_64[r], (long long)off);
				if (seh) vtext_format_append(b, "\t.seh_savereg %%%s, %lld\n", x64_register_names_64[r], (long long)off);
				off += 8;
			}
		vtext_format_append(b, "\tleaq\t%lld(%%rsp), %%rbp\n", (long long)S);
	} else {
		vtext_string_append(b, "\tmovq\t%rsp, %rbp\n");
	}
	if (seh)
		vtext_format_append(b, "\t.seh_setframe %%rbp, %lld\n\t.seh_endprologue\n", (long long)S);
	if (A > 16) {
		vtext_format_append(b, "\tandq\t$%lld, %%rsp\n\tmovq\t%%rsp, %%r12\n", -(long long)A);
	}
	if (win && frame >= 4096) {
		/* a page or more: committed a page at a time (clang's sequence) */
		if (frame > UINT32_MAX)
			vtext_format_append(b, "\tmovabsq\t$%lld, %%rax\n", (long long)frame);
		else
			vtext_format_append(b, "\tmovl\t$%lld, %%eax\n", (long long)frame);
		vtext_string_append(b, "\tcallq\t___chkstk_ms\n\tsubq\t%rax, %rsp\n");
	} else if (frame > INT32_MAX) {
		vtext_format_append(b, "\tmovabsq\t$%lld, %%r11\n\tsubq\t%%r11, %%rsp\n", (long long)frame);
	} else if (frame) {
		vtext_format_append(b, "\tsubq\t$%lld, %%rsp\n", (long long)frame);
	}
}

/* the saved registers back, then the epilogue the unwinder recognizes: lea rsp from rbp, pop */
static void x64w_epilogue(codegen_context *e, tbuf *b, uint64_t saves, bool aftercall)
{
	bool seh = CODEGEN_IS_WIN64(e->compile_context) && !e->compile_context->o->no_seh;
	int64_t S = e->x64_context->wsave, off = 0;

	for (int r = XMM0; r <= XMM15; r++)
		if (saves & XW_BIT(r)) {
			vtext_format_append(b, "\tmovaps\t%lld(%%rbp), %%xmm%d\n", (long long)(off - S), r - XMM0);
			off += 16;
		}
	for (int r = 0; r < 16; r++)
		if (saves & XW_BIT(r)) {
			vtext_format_append(b, "\tmovq\t%lld(%%rbp), %%%s\n", (long long)(off - S), x64_register_names_64[r]);
			off += 8;
		}
	if (seh && !saves && aftercall)
		vtext_string_append(b, "\tnop\n");	/* (a return address is never an epilogue's first instruction, as LLVM) */
	vtext_string_append(b, seh ? "\t.seh_startepilogue\n\tleaq\t0(%rbp), %rsp\n\tpopq\t%rbp\n\t.seh_endepilogue\n"
			     : "\tleaq\t0(%rbp), %rsp\n\tpopq\t%rbp\n");
}

/* Windows: the function's jump tables (a label, then X64_OP_JTE words) in .rdata, as clang: the unwinder
   decodes .text, and a table there could read as an epilogue */
static void x64w_tables(codegen_context *e, tbuf *b, uint32_t s)
{
	x64_lowering_context *X = e->x64_context;
	bool any = false;

	for (uint32_t k = 0; k < X->record_count; k++) {
		const record *i = &X->records[k];

		if (i->opcode == X64_OP_LABEL && k + 1 < X->record_count && X->records[k + 1].opcode == X64_OP_JTE) {
			if (!any)
				vtext_string_append(b, "\t.section\t.rdata,\"dr\"\n");
			any = true;
			vtext_string_append(b, "\t.p2align\t2\n");
			x64_label_name_append(b, s, i->label);
			vtext_string_append(b, ":\n");
		} else if (i->opcode == X64_OP_JTE) {
			vtext_string_append(b, "\t.long\t");
			x64_label_name_append(b, s, i->label);
			vtext_char_append(b, '-');
			x64_label_name_append(b, s, i->reference);
			vtext_char_append(b, '\n');
		}
	}
}

/* Windows: the pointer a symbol is reached through (__imp_NAME, .refptr.NAME; the tail emits the
   .refptr. stubs this marks) */
static void x64w_indirect(codegen_context *e, tbuf *b, uint32_t reference)
{
	cc *c = e->compile_context;
	uint32_t s = codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(reference));

	if (sym_win64_indirect(c, reference) == WIN64_IMP) {
		vtext_string_append(b, "__imp_");
	} else {
		vtext_string_append(b, ".refptr.");
		if (e->unit->refptr && !atomic_load_explicit(&e->unit->refptr[s], memory_order_relaxed))
			atomic_store_explicit(&e->unit->refptr[s], 1, memory_order_relaxed);
	}
	codegen_reference_name_append(c, b, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, s));
}

/* R12 first keeps its incoming value at -8(%rbp), as for normal realignment. */
static const int x64_interrupt_saved_registers[14] = { R12, RAX, RCX, RDX, RBX, RSI, RDI, R8, R9, R10, R11, R13, R14, R15 };

VPRIV void x64_function_print(codegen_context *e, tbuf *b, uint32_t s)
{
	cc *c = e->compile_context;
	x64_lowering_context *X = e->x64_context;
	static const char *const alu[] = { [X64_OP_ADD] = "add", [X64_OP_SUB] = "sub", [X64_OP_AND] = "and", [X64_OP_OR] = "or", [X64_OP_XOR] = "xor",
					   [X64_OP_IMUL] = "imul", [X64_OP_ADC] = "adc", [X64_OP_SBB] = "sbb", [X64_OP_ADDI] = "add", [X64_OP_SUBI] = "sub", [X64_OP_ANDI] = "and", [X64_OP_ORI] = "or",
					   [X64_OP_XORI] = "xor", [X64_OP_SHLI] = "shl", [X64_OP_SHRI] = "shr", [X64_OP_SARI] = "sar", [X64_OP_SHL] = "shl",
					   [X64_OP_SHR] = "shr", [X64_OP_SAR] = "sar", [X64_OP_NEG] = "neg", [X64_OP_NOT] = "not" };
	int64_t frame = (e->walk_state->frame_offset + 15) & ~(int64_t)15;
	uint32_t A = e->walk_state->frame_alignment;
	bool uintr = x86_feature_has(cc_x86_features_get(c, s), X86_FEATURE_UINTR);
	bool cfi = e->unit->options->cfi && !X->winframe;	/* unwind tables: .cfi_* as clang writes them (Windows: .seh_*) */
	uint32_t dj = 0;		/* -g: the next row */
	uint32_t tail = x64_tail_ref(e);
	size_t objitem = codegen_item_open(e, CODEGEN_ITEM_TEXT);
	bool objcode = false;
	if(X->stack_pointer_frame && (frame || e->has_calls))frame+=8;
	X->frame_size=frame;X->stack_pointer_delta=0;
	uint64_t wsaves = 0;	/* the Windows frame's saved registers */
	bool seh = false, aftercall = false;
	X->wsave = 0;
	if (X->winframe && !X->interrupt) {
		if (X->stack_pointer_frame)
			codegen_fatal_report(e, e->current_location, "internal error: x86-64: a Windows frame without a frame pointer");
		wsaves = x64w_saves(e);
		if (wsaves)
			X->frame_free = false;
		X->wsave = x64w_area(wsaves);
		seh = CODEGEN_IS_WIN64(c) && !X->frame_free && !c->o->no_seh;
	}
	if (seh) {
		vtext_string_append(b, ".seh_proc ");
		codegen_symbol_name_append(c, b, s);
		vtext_char_append(b, '\n');
	}

	if (unlikely(e->unit->options->debug)) {
		/* the function's own line (its body's '{', column 0), as clang */
		uint32_t f, l, cl;

		if (codegen_debug_location_get(e, c->syms[s].body ? N(c->syms[s].body)->loc : c->syms[s].loc, &f, &l, &cl))
			vtext_format_append(b, "\t.loc\t%u %u 0\n", f, l);
	}
	if (cfi)
		vtext_string_append(b, "\t.cfi_startproc\n");
	if (e->unit->options->xcf & 1)
		vtext_string_append(b, "\tendbr64\n");
	for (int k = e->unit->options->pfe_m; k < e->unit->options->pfe_n; k++)
		vtext_string_append(b, "\tnop\n");
	if (X->interrupt) {
		int cfa = X->interrupt == 2 ? 24 : 16;
		if (cfi) {
			vtext_string_append(b, "\t.cfi_signal_frame\n");
			vtext_format_append(b, "\t.cfi_def_cfa %%rsp, %d\n\t.cfi_offset %%rsp, %d\n", cfa - 8, uintr ? 8 : 16);
		}
		vtext_string_append(b, "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n");
		if (cfi) {
			vtext_format_append(b, "\t.cfi_def_cfa %%rbp, %d\n\t.cfi_offset %%rbp, %d\n", cfa, -cfa);
			vtext_format_append(b, "\t.cfi_offset %%rsp, %d\n", uintr ? 8 : 16);
		}
		int off = 0;
		for (int k = 0; k < 14; k++) {
			int r = x64_interrupt_saved_registers[k];
			vtext_format_append(b, "\tpushq\t%%%s\n", x64_register_names_64[r]);
			off -= 8;
			if (cfi) vtext_format_append(b, "\t.cfi_offset %%%s, %d\n", x64_register_names_64[r], off - cfa);
		}
		vtext_format_append(b, "\tandq\t$-%u, %%rsp\n", A);
		if (!x64_target_has_no_simd(e)) {
			/* Keep the complete legacy FP state, including a possibly full x87
			   stack, MXCSR and all sixteen XMM registers. Reserve a multiple
			   of the local frame alignment so R12 stays suitably aligned. */
			uint32_t fpframe = A > 512 ? A : 512;
			vtext_format_append(b, "\tsubq\t$%u, %%rsp\n", fpframe);
			vtext_string_append(b, "\t.byte 0x48, 0x0f, 0xae, 0x04, 0x24 # fxsave64 (%rsp)\n\tfninit\n\tfldcw\t(%rsp)\n");
		}
		vtext_string_append(b, "\tmovq\t%rsp, %r12\n\tcld\n");
		if (cfi && !x64_target_has_no_simd(e))
			for (int k = 0; k < 16; k++) x64_callee_save_cfi_append(b, 17 + k, 160 + 16 * k);
	} else if (X->winframe) {
		if (!X->frame_free)
			x64w_prologue(e, b, s, wsaves, frame, A);
	} else {
		if (!X->frame_free && !X->stack_pointer_frame)
			vtext_string_append(b, cfi ? "\tpushq\t%rbp\n\t.cfi_def_cfa_offset 16\n\t.cfi_offset %rbp, -16\n"
				      "\tmovq\t%rsp, %rbp\n\t.cfi_def_cfa_register %rbp\n" : "\tpushq\t%rbp\n\tmovq\t%rsp, %rbp\n");
		if (A > 16) {
			vtext_string_append(b, "\tpushq\t%r12\n");
			if (cfi) vtext_string_append(b, "\t.cfi_offset %r12, -24\n");
			vtext_string_append(b, "\tandq\t$");
			vtext_signed_append(b, -(int64_t)A);
			vtext_string_append(b, ", %rsp\n\tmovq\t%rsp, %r12\n");
		}
	}
	if (X->winframe && !X->interrupt) {
		/* (x64w_prologue) */
	} else if (frame > INT32_MAX) {
		vtext_string_append(b, "\tmovabsq\t$");
		vtext_signed_append(b, frame);
		vtext_string_append(b, ", %r11\n\tsubq\t%r11, %rsp\n");
	} else if (frame) {
		vtext_string_append(b, "\tsubq\t$");
		vtext_signed_append(b, frame);
		vtext_string_append(b, ", %rsp\n");
	}
	if(cfi && X->stack_pointer_frame && frame)vtext_format_append(b,"\t.cfi_def_cfa_offset %lld\n",(long long)frame+8);
	if (e->walk_state->stack_pointer_needed) {
		/* -fstack-protector: the guard into the canary slot (r11: no argument lives there) */
		x64_stack_guard_append(e, b);
		vtext_format_append(b, "\tmovq\t%%r11, %lld(%s)\n", (long long)(e->walk_state->canary - (A > 16 ? 0 : X->wsave)), A > 16 ? "%r12" : "%rbp");
	}
	for (int r = 0; r < 16; r++)
		if (X->callee_save_slots[r]) {
			x64_callee_save_access_append(e, b, r, false);
			if (cfi) {
				if (A > 16) x64_callee_save_cfi_append(b, r, X->callee_save_slots[r]);
				else vtext_format_append(b, "\t.cfi_offset %%%s, %lld\n", x64_register_names_64[r], (long long)(X->callee_save_slots[r] - 16));
			}
		}
	for (uint32_t k = 0; k < X->record_count; k++) {
		const record *i = &X->records[k];
		int sz = i->size;

		if (i->opcode != X64_OP_LABEL && i->opcode != X64_OP_TAIL)
			aftercall = i->opcode == X64_OP_CALL || i->opcode == X64_OP_CALLR;

		if (objcode && dj < e->debug_row_count && e->debug_rows[dj].record_index == k) {
			codegen_item_close(e, objitem);
			objitem = codegen_item_open(e, CODEGEN_ITEM_TEXT);
			objcode = false;
		}
		for (; dj < e->debug_row_count && e->debug_rows[dj].record_index == k; dj++)
			if (dj + 1 == e->debug_row_count || e->debug_rows[dj + 1].record_index != k)	/* the last of several at one place */
				vtext_format_append(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
				     e->debug_rows[dj].file, e->debug_rows[dj].line, e->debug_rows[dj].column);

		if (CODEGEN_IS_WIN64(c) && (i->opcode == X64_OP_JTE || (i->opcode == X64_OP_LABEL && k + 1 < X->record_count && X->records[k + 1].opcode == X64_OP_JTE)))
			continue;	/* Windows: jump tables in .rdata after the function (x64w_tables), code only in .text */
		if (codegen_context_is_direct(e)) {
			record encoded;
			bool code = x64_record_object_prepare(e, i, &encoded);
			if (code != objcode) {
				codegen_item_close(e, objitem);
				objitem = codegen_item_open(e, code ? CODEGEN_ITEM_X64 : CODEGEN_ITEM_TEXT);
				if (code) {
					uint32_t *h = codegen_item_room_need(e, 8);
					h[0] = s; h[1] = 0;
				}
				objcode = code;
			}
			if (code) {
				*(record *)codegen_item_room_need(e, sizeof(record)) = encoded;
				continue;
			}
		}

		switch (i->opcode) {
		case X64_OP_LABEL:
			x64_label_name_append(b, s, i->label);
			vtext_string_append(b, ":\n");
			continue;
		case X64_OP_MOV:
			if (X64_REGISTER_IS_XMM(i->registers[0]) && X64_REGISTER_IS_XMM(i->registers[1]))
				vtext_string_append(b, "\tmovaps\t");
			else if (X64_REGISTER_IS_XMM(i->registers[0]) || X64_REGISTER_IS_XMM(i->registers[1]))
				vtext_string_append(b, sz == 4 ? "\tmovd\t" : "\tmovq\t");
			else
				x64_mnemonic_append(b, "mov", sz);
			x64_register_name_append(b, i->registers[1], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_MOVI: {
			int64_t v = i->immediate;

			if (sz == 4) {
				x64_mnemonic_append(b, "mov", 4);
				x64_immediate_append(b, (int32_t)v);
				vtext_string_append(b, ", ");
				x64_register_name_append(b, i->registers[0], 4);
			} else if (v >= INT32_MIN && v <= INT32_MAX) {
				x64_mnemonic_append(b, "mov", 8);
				x64_immediate_append(b, v);
				vtext_string_append(b, ", ");
				x64_register_name_append(b, i->registers[0], 8);
			} else if ((uint64_t)v <= UINT32_MAX) {
				x64_mnemonic_append(b, "mov", 4);
				x64_immediate_append(b, v);
				vtext_string_append(b, ", ");
				x64_register_name_append(b, i->registers[0], 4);
			} else {
				x64_mnemonic_append(b, "movabs", 8);
				x64_immediate_append(b, v);
				vtext_string_append(b, ", ");
				x64_register_name_append(b, i->registers[0], 8);
			}
			break;
		}
		case X64_OP_LDX: case X64_OP_LD:
			if (X64_REGISTER_IS_XMM(i->registers[0])) {
				vtext_string_append(b, sz == 4 ? "\tmovss\t" : sz == 8 ? "\tmovsd\t" : "\tmovups\t");
				x64_memory_operand_append(e, b, i);
				vtext_string_append(b, ", ");
				x64_register_name_append(b, i->registers[0], 8);
				break;
			}
			{
			static const char *const sx[] = { [1] = "\tmovsbq\t", [2] = "\tmovswq\t", [4] = "\tmovslq\t", [8] = "\tmovq\t" };
			static const char *const zx[] = { [1] = "\tmovzbl\t", [2] = "\tmovzwl\t", [4] = "\tmovl\t", [8] = "\tmovq\t" };

			vtext_string_append(b, i->condition ? sx[sz] : zx[sz]);
			x64_memory_operand_append(e, b, i);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], i->condition || sz == 8 ? 8 : 4);
			break;
			}
		case X64_OP_STX: case X64_OP_ST:
			if (X64_REGISTER_IS_XMM(i->registers[2]))
				vtext_string_append(b, sz == 4 ? "\tmovss\t" : sz == 8 ? "\tmovsd\t" : "\tmovups\t");
			else
				x64_mnemonic_append(b, "mov", sz);
			x64_register_name_append(b, i->registers[2], sz);
			vtext_string_append(b, ", ");
			x64_memory_operand_append(e, b, i);
			break;
		case X64_OP_LEA:
			x64_mnemonic_append(b, "lea", sz);
			x64_memory_operand_append(e, b, i);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_LEASCALE:
			x64_mnemonic_append(b, "lea", sz);
			vtext_char_append(b, '(');
			x64_register_name_append(b, i->registers[1], 8);
			vtext_char_append(b, ',');
			x64_register_name_append(b, i->registers[2], 8);
			vtext_char_append(b, ',');
			vtext_unsigned_append(b, 1u << i->shift);
			vtext_string_append(b, "), ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_ADD: case X64_OP_SUB: case X64_OP_AND: case X64_OP_OR: case X64_OP_XOR: case X64_OP_IMUL: case X64_OP_ADC: case X64_OP_SBB:
			x64_mnemonic_append(b, alu[i->opcode], sz);
			x64_register_name_append(b, i->registers[2], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_ADDI: case X64_OP_SUBI: case X64_OP_ANDI: case X64_OP_ORI: case X64_OP_XORI: case X64_OP_SHLI: case X64_OP_SHRI: case X64_OP_SARI:
			x64_mnemonic_append(b, alu[i->opcode], sz);
			x64_immediate_append(b, i->immediate);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_IMULI:
			x64_mnemonic_append(b, "imul", sz);
			x64_immediate_append(b, i->immediate);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[1], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_SHL: case X64_OP_SHR: case X64_OP_SAR:
			x64_mnemonic_append(b, alu[i->opcode], sz);
			vtext_string_append(b, "%cl, ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_NEG: case X64_OP_NOT:
			x64_mnemonic_append(b, alu[i->opcode], sz);
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_CMP:
			x64_mnemonic_append(b, "cmp", sz);
			x64_register_name_append(b, i->registers[2], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[1], sz);
			break;
		case X64_OP_CMPI:
			x64_mnemonic_append(b, "cmp", sz);
			x64_immediate_append(b, i->immediate);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[1], sz);
			break;
		case X64_OP_TEST:
			x64_mnemonic_append(b, "test", sz);
			x64_register_name_append(b, i->registers[1], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[1], sz);
			break;
		case X64_OP_CMOV:
			vtext_string_append(b,"\tcmov");vtext_string_append(b,x64_condition_names[i->condition]);vtext_char_append(b,x64_size_suffix(sz));vtext_char_append(b,'\t');
			x64_register_name_append(b,i->registers[2],sz);vtext_string_append(b,", ");x64_register_name_append(b,i->registers[0],sz);break;
		case X64_OP_CMPM:
			x64_mnemonic_append(b,"cmp",sz);
			if(i->condition) {x64_memory_operand_append(e,b,i);vtext_string_append(b,", ");x64_register_name_append(b,i->registers[2],sz);}
			else {
				if(i->registers[2]==X64_REGISTER_NONE)x64_immediate_append(b,sz==1?(uint8_t)i->reference:sz==2?(uint16_t)i->reference:(int32_t)i->reference);
				else x64_register_name_append(b,i->registers[2],sz);
				vtext_string_append(b,", ");x64_memory_operand_append(e,b,i);
			}
			break;
		case X64_OP_TESTM:
			x64_mnemonic_append(b,"test",sz);
			x64_immediate_append(b,sz==1?(uint8_t)i->reference:sz==2?(uint16_t)i->reference:(int32_t)i->reference);
			vtext_string_append(b,", ");x64_memory_operand_append(e,b,i);break;
		case X64_OP_SETCC:
			vtext_string_append(b, "\tset");
			vtext_string_append(b, x64_condition_names[i->condition]);
			vtext_char_append(b, '\t');
			x64_register_name_append(b, i->registers[0], 1);
			vtext_string_append(b, "\n\tmovzbl\t");
			x64_register_name_append(b, i->registers[0], 1);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], 4);
			break;
		case X64_OP_JMP:
			vtext_string_append(b, "\tjmp\t");
			x64_label_name_append(b, s, i->label);
			break;
		case X64_OP_JCC:
			vtext_string_append(b, "\tj");
			vtext_string_append(b, x64_condition_names[i->condition]);
			vtext_char_append(b, '\t');
			x64_label_name_append(b, s, i->label);
			break;
		case X64_OP_TAIL:
			continue; /* argument uses stay in records; branch follows the epilogue */
		case X64_OP_CALL:
			if (CODEGEN_IS_WIN64(c) && codegen_symbol_needs_got(c, i->reference)) {
				vtext_string_append(b, "\tcallq\t*");
				x64w_indirect(e, b, i->reference);
				vtext_string_append(b, "(%rip)");
				break;
			}
			vtext_string_append(b, "\tcallq\t");
			codegen_reference_name_append(c, b, i->reference);
			if (c->o->pic && (CODEGEN_REFERENCE_KIND(i->reference) == CODEGEN_REFERENCE_NAME || codegen_symbol_needs_got(c, i->reference)))
				vtext_string_append(b, "@PLT");
			break;
		case X64_OP_CALLR:
			if (e->unit->options->xcf & 4) {	/* -mretpoline-external-thunk */
				vtext_string_append(b, (e->unit->options->xcf & 8) && i->registers[1] >= 8 ? "\tcs\n\tcallq\t__x86_indirect_thunk_" : "\tcallq\t__x86_indirect_thunk_");
				vtext_string_append(b, x64_register_names_64[i->registers[1]]);
				break;
			}
			vtext_string_append(b, "\tcallq\t*");
			x64_register_name_append(b, i->registers[1], 8);
			break;
		case X64_OP_PUSH:
			vtext_string_append(b, "\tpushq\t");
			x64_register_name_append(b, i->registers[1], 8);
			break;
		case X64_OP_POP:
			vtext_string_append(b, "\tpopq\t");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_ADJSP:
			vtext_string_append(b, i->immediate < 0 ? "\tsubq\t$" : "\taddq\t$");
			vtext_signed_append(b, i->immediate < 0 ? -i->immediate : i->immediate);
			vtext_string_append(b, ", %rsp");
			if(X->stack_pointer_frame) {
				X->stack_pointer_delta+=i->immediate;
				if(cfi)vtext_format_append(b,"\n\t.cfi_adjust_cfa_offset %lld",(long long)-i->immediate);
			}
			break;
		case X64_OP_CQO:
			vtext_string_append(b, sz == 8 ? "\tcqto" : "\tcltd");
			break;
		case X64_OP_XCHG: case X64_OP_XADD:
			x64_mnemonic_append(b, i->opcode == X64_OP_XCHG ? "xchg" : "lock xadd", sz);
			x64_register_name_append(b, i->registers[0], sz);
			vtext_string_append(b, ", ");
			x64_memory_operand_append(e, b, i);
			break;
		case X64_OP_CMPXCHG:
			x64_mnemonic_append(b, "lock cmpxchg", sz);
			x64_register_name_append(b, i->registers[2], sz);
			vtext_string_append(b, ", ");
			x64_memory_operand_append(e, b, i);
			break;
		case X64_OP_JTE:
			vtext_string_append(b, "\t.long\t");
			x64_label_name_append(b, s, i->label);
			vtext_char_append(b, '-');
			x64_label_name_append(b, s, i->reference);
			break;
		case X64_OP_LEAL:
			vtext_string_append(b, "\tleaq\t");
			x64_label_name_append(b, s, i->label);
			vtext_string_append(b, "(%rip), ");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_BLENDVB:
			vtext_string_append(b, "\tpblendvb\t%xmm0, %xmm2, %xmm1");
			break;
		case X64_OP_PCMPSTR:
			vtext_string_append(b, i->condition & 2 ? "\tpcmpi" : "\tpcmpe");
			vtext_string_append(b, i->condition & 1 ? "stri\t$" : "strm\t$");
			vtext_unsigned_append(b, (uint8_t)i->immediate);
			vtext_string_append(b, ", %xmm1, %xmm0");
			break;
		case X64_OP_RAW: {
			if (i->condition && e->walk_state->frame_alignment > 16)
				codegen_fatal_report(e, e->current_location, "inline asm r12 operand or clobber conflicts with aligned frame base");
			vtext_string_append(b, "#APP\n");
			/* a local's base register is the printer's (0: rbp, or r12 when realigned); on Windows
			   X_ASMFRAME and its displacement, placed below the save area */
			const char *p = e->raw.p + i->offset, *end = p + i->reference;
			bool realigned = e->walk_state->frame_alignment > 16;

			while (p < end) {
				const char *q = p;

				while (q < end && *q && *q != X_ASMFRAME)
					q++;
				vtext_append(b, p, q - p);
				if (q == end)
					break;
				if (*q == X_ASMFRAME) {
					char *t;
					long long v = strtoll(q + 1, &t, 10);

					vtext_signed_append(b, X->winframe && !realigned ? v - X->wsave : v);
					p = t;
				} else {
					x64_register_name_append(b, realigned ? R12 : RBP, 8);
					p = q + 1;
				}
			}
			vtext_string_append(b, "\n#NO_APP");
			break;
		}
		case X64_OP_JMPR:
			if (e->unit->options->xcf & 4) {
				vtext_string_append(b, (e->unit->options->xcf & 8) && i->registers[1] >= 8 ? "\tcs\n\tjmp\t__x86_indirect_thunk_" : "\tjmp\t__x86_indirect_thunk_");
				vtext_string_append(b, x64_register_names_64[i->registers[1]]);
				break;
			}
			vtext_string_append(b, "\tjmpq\t*");
			x64_register_name_append(b, i->registers[1], 8);
			break;
		case X64_OP_NAMED:
			codegen_reference_name_append(c, b, i->reference);
			vtext_char_append(b, ':');
			if (e->unit->options->xcf & 1)
				vtext_string_append(b, "\n\tendbr64");	/* (&&label: an indirect jump's target) */
			break;
		case X64_OP_MFENCE:
			vtext_string_append(b, "\tmfence");
			break;
		case X64_OP_RESUME_FRAME:
			/* Match the final prologue even if alignment was discovered after
			   setjmp (or inside an inline body). Bypass memory operand rewriting. */
			if (e->walk_state->frame_alignment > 16) {
				vtext_string_append(b, "\tleaq\t-8(%rbp), %r12\n\tandq\t$");
				vtext_signed_append(b, -(int64_t)e->walk_state->frame_alignment);
				vtext_string_append(b, ", %r12");
			}
			break;
		case X64_OP_BARRIER:
			vtext_string_append(b, "\t# compiler barrier");
			break;
		case X64_OP_PROBE:
			vtext_string_append(b, "\tcallq\t___chkstk_ms");
			break;
		case X64_OP_FRAMEADDR:
			vtext_format_append(b, "\tleaq\t%lld(%%rbp), ", -(long long)X->wsave);
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_GOT:
			if (CODEGEN_IS_WIN64(c)) {
				vtext_string_append(b, "\tmovq\t");
				x64w_indirect(e, b, i->reference);
				vtext_string_append(b, "(%rip), ");
				x64_register_name_append(b, i->registers[0], 8);
				break;
			}
			vtext_string_append(b, "\tmovq\t");
			codegen_reference_offset_append(c, b, i->reference, 0);
			vtext_string_append(b, "@GOTPCREL(%rip), ");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_TLS:
			if (i->condition == 3 || i->condition == 4) {
				vtext_string_append(b, "\tmovq\t");
				if (i->condition == 3) vtext_string_append(b, "%fs:0");
				else {
					codegen_reference_offset_append(c, b, i->reference, 0);
					vtext_string_append(b, "@GOTTPOFF(%rip)");
				}
				vtext_string_append(b, ", ");
				x64_register_name_append(b, i->registers[0], 8);
				break;
			}
			if (CODEGEN_IS_WIN64(c)) {
				/* the module's block from the TEB's ThreadLocalStoragePointer, then the
				   variable's offset in the .tls section (clang's sequence) */
				vtext_string_append(b, "\tmovl\t_tls_index(%rip), %r11d\n\tmovq\t%gs:88, ");
				x64_register_name_append(b, i->registers[0], 8);
				vtext_string_append(b, "\n\tmovq\t(");
				x64_register_name_append(b, i->registers[0], 8);
				vtext_string_append(b, ",%r11,8), ");
				x64_register_name_append(b, i->registers[0], 8);
				vtext_string_append(b, "\n\tleaq\t");
				codegen_reference_offset_append(c, b, i->reference, 0);
				vtext_string_append(b, "@SECREL32(");
				x64_register_name_append(b, i->registers[0], 8);
				vtext_string_append(b, "), ");
				x64_register_name_append(b, i->registers[0], 8);
				break;
			}
			if (i->condition == 2) {
				vtext_string_append(b, "\tdata16\n\tleaq\t");
				codegen_reference_offset_append(c, b, i->reference, 0);
				vtext_string_append(b, "@TLSGD(%rip), %rdi\n\tdata16\n\tdata16\n\trex64\n\tcallq\t__tls_get_addr@PLT");
				break;
			}
			vtext_string_append(b, "\tmovq\t%fs:0, ");
			x64_register_name_append(b, i->registers[0], 8);
			vtext_string_append(b, i->condition ? "\n\taddq\t" : "\n\tleaq\t");
			codegen_reference_offset_append(c, b, i->reference, 0);
			vtext_string_append(b, i->condition ? "@GOTTPOFF(%rip), " : "@TPOFF(");
			if (!i->condition) {
				x64_register_name_append(b, i->registers[0], 8);
				vtext_string_append(b, "), ");
			}
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_UD2:
			vtext_string_append(b, "\tud2");
			break;
		case X64_OP_BSF: case X64_OP_BSR: case X64_OP_POPCNT:
			x64_mnemonic_append(b, i->opcode == X64_OP_POPCNT ? "popcnt" : i->opcode == X64_OP_BSF ? "bsf" : "bsr", sz);
			x64_register_name_append(b, i->registers[1], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_CRC32Q:
			x64_mnemonic_append(b, "crc32", sz);
			x64_register_name_append(b, i->registers[2], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_BSWAP:
			if(sz==2) {
				vtext_string_append(b,"\trorw\t$8, ");x64_register_name_append(b,i->registers[0],2);break;
			}
			x64_mnemonic_append(b, "bswap", sz);
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_MULW: case X64_OP_SMULW:
			x64_mnemonic_append(b, i->opcode == X64_OP_SMULW ? "imul" : "mul", sz);
			x64_register_name_append(b, i->registers[1], sz);
			break;
		case X64_OP_IDIV: case X64_OP_DIV:
			x64_mnemonic_append(b, i->opcode == X64_OP_IDIV ? "idiv" : "div", sz);
			x64_register_name_append(b, i->registers[1], sz);
			break;
		case X64_OP_EXT: {
			static const char *const sx[] = { [1] = "\tmovsbq\t", [2] = "\tmovswq\t", [4] = "\tmovslq\t" };
			static const char *const zx[] = { [1] = "\tmovzbl\t", [2] = "\tmovzwl\t", [4] = "\tmovl\t" };

			vtext_string_append(b, i->condition ? sx[sz] : zx[sz]);
			x64_register_name_append(b, i->registers[1], sz);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], i->condition ? 8 : 4);
			break;
		}
		case X64_OP_FADD: case X64_OP_FSUB: case X64_OP_FMUL: case X64_OP_FDIV: {
			static const char *const fo[] = { "add", "sub", "mul", "div" };

			vtext_char_append(b, '\t');
			vtext_string_append(b, fo[i->opcode - X64_OP_FADD]);
			vtext_string_append(b, sz == 4 ? "ss\t" : "sd\t");
			x64_register_name_append(b, i->registers[2], 8);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		}
		case X64_OP_FXOR:
			vtext_string_append(b, "\txorps\t");
			x64_register_name_append(b, i->registers[2], 8);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_FSQRT:
			vtext_string_append(b, sz == 4 ? "\tsqrtss\t" : "\tsqrtsd\t");
			x64_register_name_append(b, i->registers[1], 8);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_FCMP:
			vtext_string_append(b, sz == 4 ? "\tucomiss\t" : "\tucomisd\t");
			x64_register_name_append(b, i->registers[2], 8);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[1], 8);
			break;
		case X64_OP_FSETCC:
			vtext_string_append(b, i->condition ? "\tsetne\t" : "\tsete\t");
			x64_register_name_append(b, i->registers[0], 1);
			vtext_string_append(b, i->condition ? "\n\tsetp\t%r11b\n\torb\t%r11b, " : "\n\tsetnp\t%r11b\n\tandb\t%r11b, ");
			x64_register_name_append(b, i->registers[0], 1);
			vtext_string_append(b, "\n\tmovzbl\t");
			x64_register_name_append(b, i->registers[0], 1);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], 4);
			break;
		case X64_OP_CVTI2F:
			vtext_string_append(b, sz == 4 ? "\tcvtsi2ss" : "\tcvtsi2sd");
			vtext_char_append(b, i->shift == 8 ? 'q' : 'l');
			vtext_char_append(b, '\t');
			x64_register_name_append(b, i->registers[1], i->shift);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_CVTF2I:
			vtext_string_append(b, i->shift == 4 ? "\tcvttss2si" : "\tcvttsd2si");
			vtext_char_append(b, sz == 8 ? 'q' : 'l');
			vtext_char_append(b, '\t');
			x64_register_name_append(b, i->registers[1], 8);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], sz);
			break;
		case X64_OP_CVTF2F:
			vtext_string_append(b, sz == 8 ? "\tcvtss2sd\t" : "\tcvtsd2ss\t");
			x64_register_name_append(b, i->registers[1], 8);
			vtext_string_append(b, ", ");
			x64_register_name_append(b, i->registers[0], 8);
			break;
		case X64_OP_X87: {
			static const char *const xn[] = { [X64_X87_LDT] = "fldt", [X64_X87_STPT] = "fstpt", [X64_X87_ADD] = "faddp\t%st, %st(1)",
				[X64_X87_SUB] = "fsubrp\t%st, %st(1)", [X64_X87_MUL] = "fmulp\t%st, %st(1)", [X64_X87_DIV] = "fdivrp\t%st, %st(1)",
				[X64_X87_CHS] = "fchs", [X64_X87_UCOMIP] = "fucomip\t%st(1), %st", [X64_X87_POP] = "fstp\t%st(0)",
				[X64_X87_STCW] = "fnstcw", [X64_X87_LDCW] = "fldcw", [X64_X87_LDZ] = "fldz" };

			vtext_char_append(b, '\t');
			if (i->condition == X64_X87_LD || i->condition == X64_X87_STP)
				vtext_string_append(b, i->condition == X64_X87_LD ? (sz == 4 ? "flds" : "fldl") : (sz == 4 ? "fstps" : "fstpl"));
			else if (i->condition == X64_X87_ILD || i->condition == X64_X87_ISTP)
				vtext_string_append(b, i->condition == X64_X87_ILD ? (sz == 2 ? "filds" : sz == 4 ? "fildl" : "fildll")
							  : (sz == 2 ? "fistps" : sz == 4 ? "fistpl" : "fistpll"));
			else
				vtext_string_append(b, xn[i->condition]);
			if (i->registers[1] != X64_REGISTER_NONE) {
				vtext_char_append(b, '\t');
				x64_memory_operand_append(e, b, i);
			}
			break;
		}
		case X64_OP_REP:
			vtext_string_append(b, i->condition ? "\trep\tstosb" : "\trep\tmovsb");
			break;
		default:
			codegen_fatal_report(e, e->current_location, "internal error: x86-64 record %d", i->opcode);
		}
		vtext_char_append(b, '\n');
	}
	if (objcode) {
		codegen_item_close(e, objitem);
		objitem = codegen_item_open(e, CODEGEN_ITEM_TEXT);
	}
	if(X->stack_pointer_frame && X->stack_pointer_delta)codegen_fatal_report(e,e->current_location,"internal error: unbalanced VIR outgoing arguments");
	if (e->walk_state->stack_pointer_needed) {
		x64_stack_guard_append(e, b);
		vtext_format_append(b, "\tcmpq\t%lld(%s), %%r11\n\tjne\t.Lspfail%u\n",
				    (long long)(e->walk_state->canary - (e->walk_state->frame_alignment > 16 ? 0 : X->wsave)), e->walk_state->frame_alignment > 16 ? "%r12" : "%rbp", s);
	}
	for (int r = 0; r < 16; r++)
		if (X->callee_save_slots[r]) {
			x64_callee_save_access_append(e, b, r, true);
			if (cfi) vtext_format_append(b, "\t.cfi_same_value %%%s\n", x64_register_names_64[r]);
		}
	for (; dj < e->debug_row_count; dj++)
		if (dj + 1 == e->debug_row_count)
			vtext_format_append(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
			     e->debug_rows[dj].file, e->debug_rows[dj].line, e->debug_rows[dj].column);
	if (X->interrupt) {
		if (!x64_target_has_no_simd(e))
			vtext_string_append(b, "\t.byte 0x49, 0x0f, 0xae, 0x0c, 0x24 # fxrstor64 (%r12)\n");
		if (cfi && !x64_target_has_no_simd(e))
			for (int k = 0; k < 16; k++) vtext_format_append(b, "\t.cfi_escape 0x08, %u\n", 17 + k);
		vtext_string_append(b, "\tleaq\t-112(%rbp), %rsp\n");
		for (int k = 13; k >= 0; k--) {
			int r = x64_interrupt_saved_registers[k];
			vtext_format_append(b, "\tpopq\t%%%s\n", x64_register_names_64[r]);
			if (cfi) vtext_format_append(b, "\t.cfi_same_value %%%s\n", x64_register_names_64[r]);
		}
		vtext_string_append(b, "\tpopq\t%rbp\n");
		if (cfi) vtext_format_append(b, "\t.cfi_def_cfa %%rsp, %d\n\t.cfi_same_value %%rbp\n", X->interrupt == 2 ? 16 : 8);
		if (X->interrupt == 2) vtext_string_append(b, "\taddq\t$8, %rsp\n");
		if (cfi) vtext_string_append(b, "\t.cfi_def_cfa %rsp, 8\n\t.cfi_same_value %rbp\n");
		/* Encodings accepted by the integrated assembler as well as GNU as. */
		vtext_string_append(b, uintr ? "\t.byte 0xf3, 0x0f, 0x01, 0xec # uiret\n" : "\t.byte 0x48, 0xcf # iretq\n");
	} else {
		if (e->walk_state->frame_alignment > 16 && !X->winframe) {
			vtext_string_append(b, "\tmovq\t-8(%rbp), %r12\n");
			if (cfi) vtext_string_append(b, "\t.cfi_same_value %r12\n");
		}
		if (X->winframe) {
			if (!X->frame_free)
				x64w_epilogue(e, b, wsaves, aftercall);
		} else if(X->stack_pointer_frame) {
			if(frame)vtext_format_append(b,"\taddq\t$%lld, %%rsp\n",(long long)frame);
			if(cfi && frame)vtext_string_append(b,"\t.cfi_def_cfa_offset 8\n");
		} else if (!X->frame_free)
			vtext_string_append(b, cfi ? "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n\t.cfi_def_cfa %rsp, 8\n" : "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n");
		if (tail) {
			vtext_string_append(b, "\tjmp\t");
			codegen_reference_name_append(c, b, tail);
			if (c->o->pic && codegen_symbol_needs_got(c, tail)) vtext_string_append(b, "@PLT");
			vtext_char_append(b, '\n');
		} else {
			/* Sibling jumps are call edges; interrupts retain their saved-frame return. */
			vtext_string_append(b, (e->unit->options->xcf & 2) ? ((e->unit->options->xcf & 8) ? "\tcs\n\tjmp\t__x86_return_thunk\n" : "\tjmp\t__x86_return_thunk\n") : "\tretq\n");
		}
	}
	if (e->walk_state->stack_pointer_needed)
		vtext_format_append(b, ".Lspfail%u:\n\tcallq\t__stack_chk_fail%s\n", s, e->unit->options->pic ? "@PLT" : "");
	if (seh)
		vtext_string_append(b, "\t.seh_endproc\n");
	vtext_string_append(b, cfi ? "\t.cfi_endproc\n.Lfunc_end" : ".Lfunc_end");
	vtext_unsigned_append(b, s);
	if (CODEGEN_IS_WIN64(c)) {	/* (COFF: no .size) */
		vtext_string_append(b, ":\n");
		x64w_tables(e, b, s);
		codegen_item_close(e, objitem);
		return;
	}
	vtext_string_append(b, ":\n\t.size\t");
	codegen_symbol_name_append(c, b, s);
	vtext_string_append(b, ", .Lfunc_end");
	vtext_unsigned_append(b, s);
	vtext_char_append(b, '-');
	codegen_symbol_name_append(c, b, s);
	vtext_char_append(b, '\n');
	codegen_item_close(e, objitem);
}
