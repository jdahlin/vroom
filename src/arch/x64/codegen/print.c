/* vcc code generator, x86-64: two-address form and the AT&T printer (x64impl.h) */
#include "x64impl.h"

/* ---------------- two-address form ---------------- */

/* ops whose result must be their first operand on x86 */
static bool x64_tied(int op)
{
	return (op >= X_ADD && op <= X_SBB) || (op >= X_ADDI && op <= X_XORI) || (op >= X_SHLI && op <= X_SAR) ||
	       op == X_NEG || op == X_NOT || op == X_BSWAP || op == X_CMOV || (op >= X_FADD && op <= X_FXOR);
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

	X->framefree = x64_framefree_candidate(e);
	for (uint32_t k = 0; k < X->nin; k++) {
		rec i = X->in[k];

		if (e->ndl)
			dbg_remap(e, &j, k, n);	/* (-g rows: to the first record emitted for k) */
		if (copies && i.op == X_MOV && x64_copy_forward_at(e, k))
			continue;
		if (X->framefree && !x64_framefree_record(e, &i))
			X->framefree = false;
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

VPRIV void x64_reg(tbuf *b, int r, int sz)
{
	if (IS_XMM(r)) {
		vtext_string_append(b, "%xmm");
		vtext_unsigned_append(b, (uint64_t)(r - XMM0));
		return;
	}
	vtext_char_append(b, '%');
	vtext_string_append(b, sz == 8 ? x64_r64[r] : sz == 4 ? x64_r32[r] : sz == 2 ? x64_r16[r] : x64_r8[r]);
}

static char x64_sfx(int sz) { return sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b'; }

VPRIV void x64_lbl(tbuf *b, uint32_t s, uint32_t l)
{
	vtext_string_append(b, ".LBB");
	vtext_unsigned_append(b, s);
	vtext_char_append(b, '_');
	vtext_unsigned_append(b, l);
}

static void x64_memop(ecx *e, tbuf *b, const rec *i)
{
	cc *c = e->c;
	int base = i->r[1];
	int64_t off=i->off;
	if(base==XFRAME) {
		base=RSP;off+=e->x64->framesz-8-e->x64->spdelta;
	}

	/* Incoming stack arguments retain rbp; local slots use the realigned base. */
	if (base == RBP && i->off < 0 && e->wk->falign > 16)
		base = R12;
	else if (base == RBP && i->off < 0)
		off -= e->x64->wsave;	/* (the Windows frame: below the save area) */
	if (i->r[1] == RIP) {
		ref_off(c, b, i->ref, i->off);
		vtext_string_append(b, "(%rip)");
		return;
	}
	if (off)
		vtext_signed_append(b, off);
	vtext_char_append(b, '(');
	x64_reg(b, base, 8);
	if (i->op == X_LDX || i->op == X_STX || ((i->op == X_TESTM || i->op == X_CMPM) && i->r[3] != XNOREG)) {
		vtext_char_append(b, ','); x64_reg(b, i->r[3], 8);
		vtext_char_append(b, ','); vtext_unsigned_append(b, 1u << i->sh);
	}
	vtext_char_append(b, ')');
}

/* "\top" with the size suffix, then a tab */
static void x64_ins(tbuf *b, const char *op, int sz)
{
	vtext_char_append(b, '\t');
	vtext_string_append(b, op);
	if (sz)
		vtext_char_append(b, x64_sfx(sz));
	vtext_char_append(b, '\t');
}

static void x64_imm(tbuf *b, int64_t v)
{
	vtext_char_append(b, '$');
	vtext_signed_append(b, v);
}

/* the stack protector's guard into r11: %fs:40 (glibc's TCB), or as -mstack-protector-guard-* say */
static void x64_guard(ecx *e, tbuf *b)
{
	const cc_opts *o = e->E->o;

	if (o->sp_sym)
		vtext_format_append(b, "\tmovq\t%%%s:%s(%%rip), %%r11\n", o->sp_gs ? "gs" : "fs", o->sp_sym);
	else
		vtext_format_append(b, "\tmovq\t%%%s:40, %%r11\n", o->sp_gs ? "gs" : "fs");
}

/* Prologue callee-save slots can follow a frame larger than disp32. */
static void x64_cs_access(ecx *e, tbuf *b, int r, bool load)
{
	int64_t off = e->x64->csslot[r];
	int base = e->wk->falign > 16 ? R12 : RBP;
	if(e->x64->spframe) {base=RSP;off+=e->x64->framesz-8;}
	if (off < INT32_MIN || off > INT32_MAX) {
		vtext_string_append(b, "\tmovabsq\t$"); vtext_signed_append(b, off);
		vtext_string_append(b, ", %r11\n\taddq\t"); x64_reg(b, base, 8);
		vtext_string_append(b, ", %r11\n");
		base = R11, off = 0;
	}
	vtext_string_append(b, "\tmovq\t");
	if (!load) { x64_reg(b, r, 8); vtext_string_append(b, ", "); }
	if (off) vtext_signed_append(b, off);
	vtext_char_append(b, '('); x64_reg(b, base, 8); vtext_char_append(b, ')');
	if (load) { vtext_string_append(b, ", "); x64_reg(b, r, 8); }
	vtext_char_append(b, '\n');
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
		r = x64_rname(nm);
		if (r >= 0)
			m |= XW_BIT(r);
	}
	return m;
}

/* the callee-saved registers (of the function's convention) the body writes: the Windows
   prologue saves them; a call writes what its callee's convention does not preserve */
static uint64_t x64w_saves(ecx *e)
{
	xst *X = e->x64;
	uint64_t def = X->csmask;

	for (uint32_t k = 0; k < X->nin; k++) {
		const rec *i = &X->in[k];
		receffects f;

		if (i->op == X_RAW) {
			def |= x64w_textregs(e->raw.p + i->off, i->ref);
			continue;
		}
		f = x64_effects(e, i);
		if ((f.fl & OD_CALL) && (i->op == X_CALL || i->op == X_CALLR) && i->sh)
			continue;	/* a Microsoft callee keeps them all */
		def |= f.def;
	}
	if (e->wk->falign > 16)
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
static void x64w_prologue(ecx *e, tbuf *b, uint32_t s, uint64_t saves, int64_t frame, uint32_t A)
{
	cc *c = e->c;
	bool seh = WIN64(c);
	int64_t S = e->x64->wsave, off = 0;

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
				vtext_format_append(b, "\tmovq\t%%%s, %lld(%%rsp)\n", x64_r64[r], (long long)off);
				if (seh) vtext_format_append(b, "\t.seh_savereg %%%s, %lld\n", x64_r64[r], (long long)off);
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
	if (seh && frame >= 4096) {
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
static void x64w_epilogue(ecx *e, tbuf *b, uint64_t saves, bool aftercall)
{
	bool seh = WIN64(e->c);
	int64_t S = e->x64->wsave, off = 0;

	for (int r = XMM0; r <= XMM15; r++)
		if (saves & XW_BIT(r)) {
			vtext_format_append(b, "\tmovaps\t%lld(%%rbp), %%xmm%d\n", (long long)(off - S), r - XMM0);
			off += 16;
		}
	for (int r = 0; r < 16; r++)
		if (saves & XW_BIT(r)) {
			vtext_format_append(b, "\tmovq\t%lld(%%rbp), %%%s\n", (long long)(off - S), x64_r64[r]);
			off += 8;
		}
	if (seh && !saves && aftercall)
		vtext_string_append(b, "\tnop\n");	/* (a return address is never an epilogue's first instruction, as LLVM) */
	vtext_string_append(b, seh ? "\t.seh_startepilogue\n\tleaq\t0(%rbp), %rsp\n\tpopq\t%rbp\n\t.seh_endepilogue\n"
			     : "\tleaq\t0(%rbp), %rsp\n\tpopq\t%rbp\n");
}

/* Windows: the pointer a symbol is reached through (__imp_NAME, .refptr.NAME; the tail emits the
   .refptr. stubs this marks) */
static void x64w_indirect(ecx *e, tbuf *b, uint32_t ref)
{
	cc *c = e->c;
	uint32_t s = canon_sym(c, RIDX(ref));

	if (sym_win64_indirect(c, ref) == WIN64_IMP) {
		vtext_string_append(b, "__imp_");
	} else {
		vtext_string_append(b, ".refptr.");
		if (e->E->refptr && !atomic_load_explicit(&e->E->refptr[s], memory_order_relaxed))
			atomic_store_explicit(&e->E->refptr[s], 1, memory_order_relaxed);
	}
	ref_name(c, b, REF(RK_SYM, s));
}

/* R12 first keeps its incoming value at -8(%rbp), as for normal realignment. */
static const int interrupt_saved[14] = { R12, RAX, RCX, RDX, RBX, RSI, RDI, R8, R9, R10, R11, R13, R14, R15 };

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
	bool uintr = x86_feature_has(cc_x86_features(c, s), X86_FEATURE_UINTR);
	bool cfi = e->E->o->cfi && !X->winframe;	/* unwind tables: .cfi_* as clang writes them (Windows: .seh_*) */
	uint32_t dj = 0;		/* -g: the next row */
	uint32_t tail = x64_tail_ref(e);
	size_t objitem = it_open(e, IT_TEXT);
	bool objcode = false;
	if(X->spframe && (frame || e->calls))frame+=8;
	X->framesz=frame;X->spdelta=0;
	uint64_t wsaves = 0;	/* the Windows frame's saved registers */
	bool seh = false, aftercall = false;
	X->wsave = 0;
	if (X->winframe && !X->interrupt) {
		if (X->spframe)
			efatal(e, e->curloc, "internal error: x86-64: a Windows frame without a frame pointer");
		wsaves = x64w_saves(e);
		if (wsaves)
			X->framefree = false;
		X->wsave = x64w_area(wsaves);
		seh = WIN64(c) && !X->framefree;
	}
	if (seh) {
		vtext_string_append(b, ".seh_proc ");
		sym_name(c, b, s);
		vtext_char_append(b, '\n');
	}

	if (unlikely(e->E->o->debug)) {
		/* the function's own line (its body's '{', column 0), as clang */
		uint32_t f, l, cl;

		if (dbg_where(e, c->syms[s].body ? N(c->syms[s].body)->loc : c->syms[s].loc, &f, &l, &cl))
			vtext_format_append(b, "\t.loc\t%u %u 0\n", f, l);
	}
	if (cfi)
		vtext_string_append(b, "\t.cfi_startproc\n");
	if (e->E->o->xcf & 1)
		vtext_string_append(b, "\tendbr64\n");
	for (int k = e->E->o->pfe_m; k < e->E->o->pfe_n; k++)
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
			int r = interrupt_saved[k];
			vtext_format_append(b, "\tpushq\t%%%s\n", x64_r64[r]);
			off -= 8;
			if (cfi) vtext_format_append(b, "\t.cfi_offset %%%s, %d\n", x64_r64[r], off - cfa);
		}
		vtext_format_append(b, "\tandq\t$-%u, %%rsp\n", A);
		if (!x64_nosimd(e)) {
			/* Keep the complete legacy FP state, including a possibly full x87
			   stack, MXCSR and all sixteen XMM registers. Reserve a multiple
			   of the local frame alignment so R12 stays suitably aligned. */
			uint32_t fpframe = A > 512 ? A : 512;
			vtext_format_append(b, "\tsubq\t$%u, %%rsp\n", fpframe);
			vtext_string_append(b, "\t.byte 0x48, 0x0f, 0xae, 0x04, 0x24 # fxsave64 (%rsp)\n\tfninit\n\tfldcw\t(%rsp)\n");
		}
		vtext_string_append(b, "\tmovq\t%rsp, %r12\n\tcld\n");
		if (cfi && !x64_nosimd(e))
			for (int k = 0; k < 16; k++) x64_cs_cfi(b, 17 + k, 160 + 16 * k);
	} else if (X->winframe) {
		if (!X->framefree)
			x64w_prologue(e, b, s, wsaves, frame, A);
	} else {
		if (!X->framefree && !X->spframe)
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
	if(cfi && X->spframe && frame)vtext_format_append(b,"\t.cfi_def_cfa_offset %lld\n",(long long)frame+8);
	if (e->wk->spneed) {
		/* -fstack-protector: the guard into the canary slot (r11: no argument lives there) */
		x64_guard(e, b);
		vtext_format_append(b, "\tmovq\t%%r11, %lld(%s)\n", (long long)(e->wk->canary - (A > 16 ? 0 : X->wsave)), A > 16 ? "%r12" : "%rbp");
	}
	for (int r = 0; r < 16; r++)
		if (X->csslot[r]) {
			x64_cs_access(e, b, r, false);
			if (cfi) {
				if (A > 16) x64_cs_cfi(b, r, X->csslot[r]);
				else vtext_format_append(b, "\t.cfi_offset %%%s, %lld\n", x64_r64[r], (long long)(X->csslot[r] - 16));
			}
		}
	for (uint32_t k = 0; k < X->nin; k++) {
		const rec *i = &X->in[k];
		int sz = i->sz;

		if (i->op != X_LABEL && i->op != X_TAIL)
			aftercall = i->op == X_CALL || i->op == X_CALLR;

		if (objcode && dj < e->ndl && e->dl[dj].k == k) {
			it_close(e, objitem);
			objitem = it_open(e, IT_TEXT);
			objcode = false;
		}
		for (; dj < e->ndl && e->dl[dj].k == k; dj++)
			if (dj + 1 == e->ndl || e->dl[dj + 1].k != k)	/* the last of several at one place */
				vtext_format_append(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
				     e->dl[dj].file, e->dl[dj].line, e->dl[dj].col);

		if (direct(e)) {
			rec encoded;
			bool code = x64_obj_record(e, i, &encoded);
			if (code != objcode) {
				it_close(e, objitem);
				objitem = it_open(e, code ? IT_X64 : IT_TEXT);
				if (code) {
					uint32_t *h = it_room(e, 8);
					h[0] = s; h[1] = 0;
				}
				objcode = code;
			}
			if (code) {
				*(rec *)it_room(e, sizeof(rec)) = encoded;
				continue;
			}
		}

		switch (i->op) {
		case X_LABEL:
			x64_lbl(b, s, i->label);
			vtext_string_append(b, ":\n");
			continue;
		case X_MOV:
			if (IS_XMM(i->r[0]) && IS_XMM(i->r[1]))
				vtext_string_append(b, "\tmovaps\t");
			else if (IS_XMM(i->r[0]) || IS_XMM(i->r[1]))
				vtext_string_append(b, sz == 4 ? "\tmovd\t" : "\tmovq\t");
			else
				x64_ins(b, "mov", sz);
			x64_reg(b, i->r[1], sz);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_MOVI: {
			int64_t v = i->imm;

			if (sz == 4) {
				x64_ins(b, "mov", 4);
				x64_imm(b, (int32_t)v);
				vtext_string_append(b, ", ");
				x64_reg(b, i->r[0], 4);
			} else if (v >= INT32_MIN && v <= INT32_MAX) {
				x64_ins(b, "mov", 8);
				x64_imm(b, v);
				vtext_string_append(b, ", ");
				x64_reg(b, i->r[0], 8);
			} else if ((uint64_t)v <= UINT32_MAX) {
				x64_ins(b, "mov", 4);
				x64_imm(b, v);
				vtext_string_append(b, ", ");
				x64_reg(b, i->r[0], 4);
			} else {
				x64_ins(b, "movabs", 8);
				x64_imm(b, v);
				vtext_string_append(b, ", ");
				x64_reg(b, i->r[0], 8);
			}
			break;
		}
		case X_LDX: case X_LD:
			if (IS_XMM(i->r[0])) {
				vtext_string_append(b, sz == 4 ? "\tmovss\t" : sz == 8 ? "\tmovsd\t" : "\tmovups\t");
				x64_memop(e, b, i);
				vtext_string_append(b, ", ");
				x64_reg(b, i->r[0], 8);
				break;
			}
			{
			static const char *const sx[] = { [1] = "\tmovsbq\t", [2] = "\tmovswq\t", [4] = "\tmovslq\t", [8] = "\tmovq\t" };
			static const char *const zx[] = { [1] = "\tmovzbl\t", [2] = "\tmovzwl\t", [4] = "\tmovl\t", [8] = "\tmovq\t" };

			vtext_string_append(b, i->cond ? sx[sz] : zx[sz]);
			x64_memop(e, b, i);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], i->cond || sz == 8 ? 8 : 4);
			break;
			}
		case X_STX: case X_ST:
			if (IS_XMM(i->r[2]))
				vtext_string_append(b, sz == 4 ? "\tmovss\t" : sz == 8 ? "\tmovsd\t" : "\tmovups\t");
			else
				x64_ins(b, "mov", sz);
			x64_reg(b, i->r[2], sz);
			vtext_string_append(b, ", ");
			x64_memop(e, b, i);
			break;
		case X_LEA:
			x64_ins(b, "lea", sz);
			x64_memop(e, b, i);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_LEASCALE:
			x64_ins(b, "lea", sz);
			vtext_char_append(b, '(');
			x64_reg(b, i->r[1], 8);
			vtext_char_append(b, ',');
			x64_reg(b, i->r[2], 8);
			vtext_char_append(b, ',');
			vtext_unsigned_append(b, 1u << i->sh);
			vtext_string_append(b, "), ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_ADD: case X_SUB: case X_AND: case X_OR: case X_XOR: case X_IMUL: case X_ADC: case X_SBB:
			x64_ins(b, alu[i->op], sz);
			x64_reg(b, i->r[2], sz);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_ADDI: case X_SUBI: case X_ANDI: case X_ORI: case X_XORI: case X_SHLI: case X_SHRI: case X_SARI:
			x64_ins(b, alu[i->op], sz);
			x64_imm(b, i->imm);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_IMULI:
			x64_ins(b, "imul", sz);
			x64_imm(b, i->imm);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[1], sz);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_SHL: case X_SHR: case X_SAR:
			x64_ins(b, alu[i->op], sz);
			vtext_string_append(b, "%cl, ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_NEG: case X_NOT:
			x64_ins(b, alu[i->op], sz);
			x64_reg(b, i->r[0], sz);
			break;
		case X_CMP:
			x64_ins(b, "cmp", sz);
			x64_reg(b, i->r[2], sz);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[1], sz);
			break;
		case X_CMPI:
			x64_ins(b, "cmp", sz);
			x64_imm(b, i->imm);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[1], sz);
			break;
		case X_TEST:
			x64_ins(b, "test", sz);
			x64_reg(b, i->r[1], sz);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[1], sz);
			break;
		case X_CMOV:
			vtext_string_append(b,"\tcmov");vtext_string_append(b,x64_ccn[i->cond]);vtext_char_append(b,x64_sfx(sz));vtext_char_append(b,'\t');
			x64_reg(b,i->r[2],sz);vtext_string_append(b,", ");x64_reg(b,i->r[0],sz);break;
		case X_CMPM:
			x64_ins(b,"cmp",sz);
			if(i->cond) {x64_memop(e,b,i);vtext_string_append(b,", ");x64_reg(b,i->r[2],sz);}
			else {
				if(i->r[2]==XNOREG)x64_imm(b,sz==1?(uint8_t)i->ref:sz==2?(uint16_t)i->ref:(int32_t)i->ref);
				else x64_reg(b,i->r[2],sz);
				vtext_string_append(b,", ");x64_memop(e,b,i);
			}
			break;
		case X_TESTM:
			x64_ins(b,"test",sz);
			x64_imm(b,sz==1?(uint8_t)i->ref:sz==2?(uint16_t)i->ref:(int32_t)i->ref);
			vtext_string_append(b,", ");x64_memop(e,b,i);break;
		case X_SETCC:
			vtext_string_append(b, "\tset");
			vtext_string_append(b, x64_ccn[i->cond]);
			vtext_char_append(b, '\t');
			x64_reg(b, i->r[0], 1);
			vtext_string_append(b, "\n\tmovzbl\t");
			x64_reg(b, i->r[0], 1);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], 4);
			break;
		case X_JMP:
			vtext_string_append(b, "\tjmp\t");
			x64_lbl(b, s, i->label);
			break;
		case X_JCC:
			vtext_string_append(b, "\tj");
			vtext_string_append(b, x64_ccn[i->cond]);
			vtext_char_append(b, '\t');
			x64_lbl(b, s, i->label);
			break;
		case X_TAIL:
			continue; /* argument uses stay in records; branch follows the epilogue */
		case X_CALL:
			if (WIN64(c) && sym_needs_got(c, i->ref)) {
				vtext_string_append(b, "\tcallq\t*");
				x64w_indirect(e, b, i->ref);
				vtext_string_append(b, "(%rip)");
				break;
			}
			vtext_string_append(b, "\tcallq\t");
			ref_name(c, b, i->ref);
			if (c->o->pic && (RKIND(i->ref) == RK_NAME || sym_needs_got(c, i->ref)))
				vtext_string_append(b, "@PLT");
			break;
		case X_CALLR:
			if (e->E->o->xcf & 4) {	/* -mretpoline-external-thunk */
				vtext_string_append(b, (e->E->o->xcf & 8) && i->r[1] >= 8 ? "\tcs\n\tcallq\t__x86_indirect_thunk_" : "\tcallq\t__x86_indirect_thunk_");
				vtext_string_append(b, x64_r64[i->r[1]]);
				break;
			}
			vtext_string_append(b, "\tcallq\t*");
			x64_reg(b, i->r[1], 8);
			break;
		case X_PUSH:
			vtext_string_append(b, "\tpushq\t");
			x64_reg(b, i->r[1], 8);
			break;
		case X_POP:
			vtext_string_append(b, "\tpopq\t");
			x64_reg(b, i->r[0], 8);
			break;
		case X_ADJSP:
			vtext_string_append(b, i->imm < 0 ? "\tsubq\t$" : "\taddq\t$");
			vtext_signed_append(b, i->imm < 0 ? -i->imm : i->imm);
			vtext_string_append(b, ", %rsp");
			if(X->spframe) {
				X->spdelta+=i->imm;
				if(cfi)vtext_format_append(b,"\n\t.cfi_adjust_cfa_offset %lld",(long long)-i->imm);
			}
			break;
		case X_CQO:
			vtext_string_append(b, sz == 8 ? "\tcqto" : "\tcltd");
			break;
		case X_XCHG: case X_XADD:
			x64_ins(b, i->op == X_XCHG ? "xchg" : "lock xadd", sz);
			x64_reg(b, i->r[0], sz);
			vtext_string_append(b, ", ");
			x64_memop(e, b, i);
			break;
		case X_CMPXCHG:
			x64_ins(b, "lock cmpxchg", sz);
			x64_reg(b, i->r[2], sz);
			vtext_string_append(b, ", ");
			x64_memop(e, b, i);
			break;
		case X_JTE:
			vtext_string_append(b, "\t.long\t");
			x64_lbl(b, s, i->label);
			vtext_char_append(b, '-');
			x64_lbl(b, s, i->ref);
			break;
		case X_LEAL:
			vtext_string_append(b, "\tleaq\t");
			x64_lbl(b, s, i->label);
			vtext_string_append(b, "(%rip), ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_BLENDVB:
			vtext_string_append(b, "\tpblendvb\t%xmm0, %xmm2, %xmm1");
			break;
		case X_PCMPSTR:
			vtext_string_append(b, i->cond & 2 ? "\tpcmpi" : "\tpcmpe");
			vtext_string_append(b, i->cond & 1 ? "stri\t$" : "strm\t$");
			vtext_unsigned_append(b, (uint8_t)i->imm);
			vtext_string_append(b, ", %xmm1, %xmm0");
			break;
		case X_RAW:
			if (i->cond && e->wk->falign > 16)
				efatal(e, e->curloc, "inline asm r12 operand or clobber conflicts with aligned frame base");
			vtext_string_append(b, "#APP\n");
			if (X->winframe && memchr(e->raw.p + i->off, X_ASMFRAME, i->ref)) {
				/* the Windows frame: a local's displacement below the save area */
				const char *p = e->raw.p + i->off, *end = p + i->ref;

				while (p < end) {
					if (*p != X_ASMFRAME) {
						vtext_char_append(b, *p++);
						continue;
					}
					char *q;
					long long v = strtoll(p + 1, &q, 10);

					vtext_signed_append(b, v - X->wsave);
					p = q;
				}
			} else {
				vtext_append(b, e->raw.p + i->off, i->ref);
			}
			vtext_string_append(b, "\n#NO_APP");
			break;
		case X_JMPR:
			if (e->E->o->xcf & 4) {
				vtext_string_append(b, (e->E->o->xcf & 8) && i->r[1] >= 8 ? "\tcs\n\tjmp\t__x86_indirect_thunk_" : "\tjmp\t__x86_indirect_thunk_");
				vtext_string_append(b, x64_r64[i->r[1]]);
				break;
			}
			vtext_string_append(b, "\tjmpq\t*");
			x64_reg(b, i->r[1], 8);
			break;
		case X_NAMED:
			ref_name(c, b, i->ref);
			vtext_char_append(b, ':');
			if (e->E->o->xcf & 1)
				vtext_string_append(b, "\n\tendbr64");	/* (&&label: an indirect jump's target) */
			break;
		case X_MFENCE:
			vtext_string_append(b, "\tmfence");
			break;
		case X_RESUME_FRAME:
			/* Match the final prologue even if alignment was discovered after
			   setjmp (or inside an inline body). Bypass memory operand rewriting. */
			if (e->wk->falign > 16) {
				vtext_string_append(b, "\tleaq\t-8(%rbp), %r12\n\tandq\t$");
				vtext_signed_append(b, -(int64_t)e->wk->falign);
				vtext_string_append(b, ", %r12");
			}
			break;
		case X_BARRIER:
			vtext_string_append(b, "\t# compiler barrier");
			break;
		case X_PROBE:
			vtext_string_append(b, "\tcallq\t___chkstk_ms");
			break;
		case X_GOT:
			if (WIN64(c)) {
				vtext_string_append(b, "\tmovq\t");
				x64w_indirect(e, b, i->ref);
				vtext_string_append(b, "(%rip), ");
				x64_reg(b, i->r[0], 8);
				break;
			}
			vtext_string_append(b, "\tmovq\t");
			ref_off(c, b, i->ref, 0);
			vtext_string_append(b, "@GOTPCREL(%rip), ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_TLS:
			if (WIN64(c)) {
				/* the module's block from the TEB's ThreadLocalStoragePointer, then the
				   variable's offset in the .tls section (clang's sequence) */
				vtext_string_append(b, "\tmovl\t_tls_index(%rip), %r11d\n\tmovq\t%gs:88, ");
				x64_reg(b, i->r[0], 8);
				vtext_string_append(b, "\n\tmovq\t(");
				x64_reg(b, i->r[0], 8);
				vtext_string_append(b, ",%r11,8), ");
				x64_reg(b, i->r[0], 8);
				vtext_string_append(b, "\n\tleaq\t");
				ref_off(c, b, i->ref, 0);
				vtext_string_append(b, "@SECREL32(");
				x64_reg(b, i->r[0], 8);
				vtext_string_append(b, "), ");
				x64_reg(b, i->r[0], 8);
				break;
			}
			if (i->cond == 2) {
				vtext_string_append(b, "\tdata16\n\tleaq\t");
				ref_off(c, b, i->ref, 0);
				vtext_string_append(b, "@TLSGD(%rip), %rdi\n\tdata16\n\tdata16\n\trex64\n\tcallq\t__tls_get_addr@PLT");
				break;
			}
			vtext_string_append(b, "\tmovq\t%fs:0, ");
			x64_reg(b, i->r[0], 8);
			vtext_string_append(b, i->cond ? "\n\taddq\t" : "\n\tleaq\t");
			ref_off(c, b, i->ref, 0);
			vtext_string_append(b, i->cond ? "@GOTTPOFF(%rip), " : "@TPOFF(");
			if (!i->cond) {
				x64_reg(b, i->r[0], 8);
				vtext_string_append(b, "), ");
			}
			x64_reg(b, i->r[0], 8);
			break;
		case X_UD2:
			vtext_string_append(b, "\tud2");
			break;
		case X_BSF: case X_BSR:
			x64_ins(b, i->op == X_BSF ? "bsf" : "bsr", sz);
			x64_reg(b, i->r[1], sz);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_BSWAP:
			if(sz==2) {
				vtext_string_append(b,"\trorw\t$8, ");x64_reg(b,i->r[0],2);break;
			}
			x64_ins(b, "bswap", sz);
			x64_reg(b, i->r[0], sz);
			break;
		case X_MULW: case X_SMULW:
			x64_ins(b, i->op == X_SMULW ? "imul" : "mul", sz);
			x64_reg(b, i->r[1], sz);
			break;
		case X_IDIV: case X_DIV:
			x64_ins(b, i->op == X_IDIV ? "idiv" : "div", sz);
			x64_reg(b, i->r[1], sz);
			break;
		case X_EXT: {
			static const char *const sx[] = { [1] = "\tmovsbq\t", [2] = "\tmovswq\t", [4] = "\tmovslq\t" };
			static const char *const zx[] = { [1] = "\tmovzbl\t", [2] = "\tmovzwl\t", [4] = "\tmovl\t" };

			vtext_string_append(b, i->cond ? sx[sz] : zx[sz]);
			x64_reg(b, i->r[1], sz);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], i->cond ? 8 : 4);
			break;
		}
		case X_FADD: case X_FSUB: case X_FMUL: case X_FDIV: {
			static const char *const fo[] = { "add", "sub", "mul", "div" };

			vtext_char_append(b, '\t');
			vtext_string_append(b, fo[i->op - X_FADD]);
			vtext_string_append(b, sz == 4 ? "ss\t" : "sd\t");
			x64_reg(b, i->r[2], 8);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		}
		case X_FXOR:
			vtext_string_append(b, "\txorps\t");
			x64_reg(b, i->r[2], 8);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_FSQRT:
			vtext_string_append(b, sz == 4 ? "\tsqrtss\t" : "\tsqrtsd\t");
			x64_reg(b, i->r[1], 8);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_FCMP:
			vtext_string_append(b, sz == 4 ? "\tucomiss\t" : "\tucomisd\t");
			x64_reg(b, i->r[2], 8);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[1], 8);
			break;
		case X_FSETCC:
			vtext_string_append(b, i->cond ? "\tsetne\t" : "\tsete\t");
			x64_reg(b, i->r[0], 1);
			vtext_string_append(b, i->cond ? "\n\tsetp\t%r11b\n\torb\t%r11b, " : "\n\tsetnp\t%r11b\n\tandb\t%r11b, ");
			x64_reg(b, i->r[0], 1);
			vtext_string_append(b, "\n\tmovzbl\t");
			x64_reg(b, i->r[0], 1);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], 4);
			break;
		case X_CVTI2F:
			vtext_string_append(b, sz == 4 ? "\tcvtsi2ss" : "\tcvtsi2sd");
			vtext_char_append(b, i->sh == 8 ? 'q' : 'l');
			vtext_char_append(b, '\t');
			x64_reg(b, i->r[1], i->sh);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_CVTF2I:
			vtext_string_append(b, i->sh == 4 ? "\tcvttss2si" : "\tcvttsd2si");
			vtext_char_append(b, sz == 8 ? 'q' : 'l');
			vtext_char_append(b, '\t');
			x64_reg(b, i->r[1], 8);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], sz);
			break;
		case X_CVTF2F:
			vtext_string_append(b, sz == 8 ? "\tcvtss2sd\t" : "\tcvtsd2ss\t");
			x64_reg(b, i->r[1], 8);
			vtext_string_append(b, ", ");
			x64_reg(b, i->r[0], 8);
			break;
		case X_X87: {
			static const char *const xn[] = { [XF_LDT] = "fldt", [XF_STPT] = "fstpt", [XF_ADD] = "faddp\t%st, %st(1)",
				[XF_SUB] = "fsubrp\t%st, %st(1)", [XF_MUL] = "fmulp\t%st, %st(1)", [XF_DIV] = "fdivrp\t%st, %st(1)",
				[XF_CHS] = "fchs", [XF_UCOMIP] = "fucomip\t%st(1), %st", [XF_POP] = "fstp\t%st(0)",
				[XF_STCW] = "fnstcw", [XF_LDCW] = "fldcw", [XF_LDZ] = "fldz" };

			vtext_char_append(b, '\t');
			if (i->cond == XF_LD || i->cond == XF_STP)
				vtext_string_append(b, i->cond == XF_LD ? (sz == 4 ? "flds" : "fldl") : (sz == 4 ? "fstps" : "fstpl"));
			else if (i->cond == XF_ILD || i->cond == XF_ISTP)
				vtext_string_append(b, i->cond == XF_ILD ? (sz == 2 ? "filds" : sz == 4 ? "fildl" : "fildll")
							  : (sz == 2 ? "fistps" : sz == 4 ? "fistpl" : "fistpll"));
			else
				vtext_string_append(b, xn[i->cond]);
			if (i->r[1] != XNOREG) {
				vtext_char_append(b, '\t');
				x64_memop(e, b, i);
			}
			break;
		}
		case X_REP:
			vtext_string_append(b, i->cond ? "\trep\tstosb" : "\trep\tmovsb");
			break;
		default:
			efatal(e, e->curloc, "internal error: x86-64 record %d", i->op);
		}
		vtext_char_append(b, '\n');
	}
	if (objcode) {
		it_close(e, objitem);
		objitem = it_open(e, IT_TEXT);
	}
	if(X->spframe && X->spdelta)efatal(e,e->curloc,"internal error: unbalanced VIR outgoing arguments");
	if (e->wk->spneed) {
		x64_guard(e, b);
		vtext_format_append(b, "\tcmpq\t%lld(%s), %%r11\n\tjne\t.Lspfail%u\n",
				    (long long)(e->wk->canary - (e->wk->falign > 16 ? 0 : X->wsave)), e->wk->falign > 16 ? "%r12" : "%rbp", s);
	}
	for (int r = 0; r < 16; r++)
		if (X->csslot[r]) {
			x64_cs_access(e, b, r, true);
			if (cfi) vtext_format_append(b, "\t.cfi_same_value %%%s\n", x64_r64[r]);
		}
	for (; dj < e->ndl; dj++)
		if (dj + 1 == e->ndl)
			vtext_format_append(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
			     e->dl[dj].file, e->dl[dj].line, e->dl[dj].col);
	if (X->interrupt) {
		if (!x64_nosimd(e))
			vtext_string_append(b, "\t.byte 0x49, 0x0f, 0xae, 0x0c, 0x24 # fxrstor64 (%r12)\n");
		if (cfi && !x64_nosimd(e))
			for (int k = 0; k < 16; k++) vtext_format_append(b, "\t.cfi_escape 0x08, %u\n", 17 + k);
		vtext_string_append(b, "\tleaq\t-112(%rbp), %rsp\n");
		for (int k = 13; k >= 0; k--) {
			int r = interrupt_saved[k];
			vtext_format_append(b, "\tpopq\t%%%s\n", x64_r64[r]);
			if (cfi) vtext_format_append(b, "\t.cfi_same_value %%%s\n", x64_r64[r]);
		}
		vtext_string_append(b, "\tpopq\t%rbp\n");
		if (cfi) vtext_format_append(b, "\t.cfi_def_cfa %%rsp, %d\n\t.cfi_same_value %%rbp\n", X->interrupt == 2 ? 16 : 8);
		if (X->interrupt == 2) vtext_string_append(b, "\taddq\t$8, %rsp\n");
		if (cfi) vtext_string_append(b, "\t.cfi_def_cfa %rsp, 8\n\t.cfi_same_value %rbp\n");
		/* Encodings accepted by the integrated assembler as well as GNU as. */
		vtext_string_append(b, uintr ? "\t.byte 0xf3, 0x0f, 0x01, 0xec # uiret\n" : "\t.byte 0x48, 0xcf # iretq\n");
	} else {
		if (e->wk->falign > 16 && !X->winframe) {
			vtext_string_append(b, "\tmovq\t-8(%rbp), %r12\n");
			if (cfi) vtext_string_append(b, "\t.cfi_same_value %r12\n");
		}
		if (X->winframe) {
			if (!X->framefree)
				x64w_epilogue(e, b, wsaves, aftercall);
		} else if(X->spframe) {
			if(frame)vtext_format_append(b,"\taddq\t$%lld, %%rsp\n",(long long)frame);
			if(cfi && frame)vtext_string_append(b,"\t.cfi_def_cfa_offset 8\n");
		} else if (!X->framefree)
			vtext_string_append(b, cfi ? "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n\t.cfi_def_cfa %rsp, 8\n" : "\tmovq\t%rbp, %rsp\n\tpopq\t%rbp\n");
		if (tail) {
			vtext_string_append(b, "\tjmp\t");
			ref_name(c, b, tail);
			if (c->o->pic && sym_needs_got(c, tail)) vtext_string_append(b, "@PLT");
			vtext_char_append(b, '\n');
		} else {
			/* Sibling jumps are call edges; interrupts retain their saved-frame return. */
			vtext_string_append(b, (e->E->o->xcf & 2) ? ((e->E->o->xcf & 8) ? "\tcs\n\tjmp\t__x86_return_thunk\n" : "\tjmp\t__x86_return_thunk\n") : "\tretq\n");
		}
	}
	if (e->wk->spneed)
		vtext_format_append(b, ".Lspfail%u:\n\tcallq\t__stack_chk_fail%s\n", s, e->E->o->pic ? "@PLT" : "");
	if (seh)
		vtext_string_append(b, "\t.seh_endproc\n");
	vtext_string_append(b, cfi ? "\t.cfi_endproc\n.Lfunc_end" : ".Lfunc_end");
	vtext_unsigned_append(b, s);
	if (WIN64(c)) {	/* (COFF: no .size) */
		vtext_string_append(b, ":\n");
		it_close(e, objitem);
		return;
	}
	vtext_string_append(b, ":\n\t.size\t");
	sym_name(c, b, s);
	vtext_string_append(b, ", .Lfunc_end");
	vtext_unsigned_append(b, s);
	vtext_char_append(b, '-');
	sym_name(c, b, s);
	vtext_char_append(b, '\n');
	it_close(e, objitem);
}
