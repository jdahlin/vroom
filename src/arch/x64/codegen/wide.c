/* vcc code generator, x86-64: __int128 (x64-internal.h).  Its values are 16-byte
 * frame objects held by address, as x87 long double's: add/adc and
 * sub/sbb, the bitwise operations and the low product inline, division,
 * variable shifts and float conversions through libgcc (__divti3 ...). */
#include "x64-internal.h"

VPRIV bool x64_i128mem(void) { return true; }

static codegen_value x64_wv(int r) { return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }; }
static codegen_lvalue x64_wat(int r, int off) { return (codegen_lvalue){ .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)r, .offset = off, .align = 8 }; }

/* a new 16-byte object: its frame slot, and its address in a temporary */
static int x64_wnew(codegen_context *e, int64_t *slot)
{
	codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 16, 16), .align = 16 };

	if (slot)
		*slot = (int64_t)l.offset;
	return x64_laddr(e, &l);
}

/* the constant's object */
VPRIV codegen_value x64_wconst(codegen_context *e, i128 v)
{
	int rd = x64_wnew(e, NULL);
	codegen_lvalue lo = x64_wat(rd, 0), hi = x64_wat(rd, 8);

	x64_record_emit_move_immediate(e, R11, (int64_t)(uint64_t)v);
	x64_record_emit_memory(e, X64_OP_ST, 8, R11, &lo, false);
	x64_record_emit_move_immediate(e, R11, (int64_t)(uint64_t)((u128)v >> 64));
	x64_record_emit_memory(e, X64_OP_ST, 8, R11, &hi, false);
	return x64_wv(rd);
}

/* an operand's address (a constant gets its object) */
static int x64_waddr(codegen_context *e, codegen_value *v)
{
	if (v->kind == CODEGEN_VALUE_CONSTANT)
		*v = x64_wconst(e, v->integer);
	return x64_toreg(e, v, 8);
}

/* Preserve the source object (which may be a const global or an lvalue):
   a bit-precise value's high word is extended in a fresh 16-byte temporary. */
VPRIV codegen_value x64_wbitint(codegen_context *e, codegen_value v, unsigned bits, bool sgn)
{
	if (bits == 128)
		return v;
	int rs = x64_waddr(e, &v), rd = x64_wnew(e, NULL);
	codegen_lvalue slo = x64_wat(rs, 0), shi = x64_wat(rs, 8);
	codegen_lvalue dlo = x64_wat(rd, 0), dhi = x64_wat(rd, 8);

	x64_record_emit_memory(e, X64_OP_LD, 8, R11, &slo, false);
	x64_record_emit_memory(e, X64_OP_ST, 8, R11, &dlo, false);
	x64_record_emit_memory(e, X64_OP_LD, 8, R11, &shi, false);
	x64_record_emit_three_registers(e, X64_OP_SHLI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 128 - bits;
	x64_record_emit_three_registers(e, sgn ? X64_OP_SARI : X64_OP_SHRI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 128 - bits;
	x64_record_emit_memory(e, X64_OP_ST, 8, R11, &dhi, false);
	x64_tfree(e, rs);
	return x64_wv(rd);
}

/* ---------------- libgcc calls ---------------- */

/*
 * fn(args): words[k] (frame slots, 8 bytes each) into the integer argument
 * registers, or xmm0 for a float argument; the live temporaries saved
 * around the call.  The result's rax, rdx (or xmm0) stored to the frame
 * slot res.
 */
static void x64_lcall(codegen_context *e, int fn, const int64_t *words, int nw, int xmmarg, int64_t res, int resw, bool fres)
{
	x64_lowering_context *X = e->x64_context;
	uint32_t saved = X->temporaries_in_use & X64_TEMPORARIES, fsaved = X->temporaries_in_use & X64_FLOAT_TEMPORARIES;
	int pad;
	record *i;

	for (int q = 0; q < 16; q++)
		if (saved & (1u << q)) {
			x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, q, X64_REGISTER_NONE);
			X->outstanding_pushes++;
		}
	for (int q = XMM8; q < XMM15; q++)
		if (fsaved & (1u << q)) {
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

			x64_record_emit(e, X64_OP_ADJSP)->immediate = -8;
			x64_record_emit_memory(e, X64_OP_ST, 8, q, &d, false);
			X->outstanding_pushes++;
		}
	pad = X->outstanding_pushes & 1;
	if (pad) {
		x64_record_emit(e, X64_OP_ADJSP)->immediate = -8;
		X->outstanding_pushes++;
	}
	for (int k = 0; k < nw; k++) {
		codegen_lvalue s = { .kind = CODEGEN_LVALUE_FRAME, .offset = words[k], .align = 8 };

		x64_record_emit_memory(e, X64_OP_LD, 8, k == xmmarg ? XMM0 : x64_argument_register(k - (xmmarg >= 0 && k > xmmarg)), &s, false);
	}
	i = x64_record_emit(e, X64_OP_CALL);
	i->reference = CODEGEN_FIXED_REFERENCE(fn);
	for (int w = 0; w < resw; w++) {
		codegen_lvalue d = { .kind = CODEGEN_LVALUE_FRAME, .offset = res + 8 * w, .align = 8 };

		x64_record_emit_memory(e, X64_OP_ST, 8, fres ? XMM0 : w ? RDX : RAX, &d, false);
	}
	if (pad) {
		x64_record_emit(e, X64_OP_ADJSP)->immediate = 8;
		X->outstanding_pushes--;
	}
	for (int q = XMM15 - 1; q >= XMM8; q--)
		if (fsaved & (1u << q)) {
			codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

			x64_record_emit_memory(e, X64_OP_LD, 8, q, &s, false);
			x64_record_emit(e, X64_OP_ADJSP)->immediate = 8;
			X->outstanding_pushes--;
		}
	for (int q = 15; q >= 0; q--)
		if (saved & (1u << q)) {
			x64_record_emit_three_registers(e, X64_OP_POP, 8, q, X64_REGISTER_NONE, X64_REGISTER_NONE);
			X->outstanding_pushes--;
		}
}

/* the 8-byte word at [r + off] copied to a new frame slot */
static int64_t x64_wword(codegen_context *e, int r, int off)
{
	codegen_lvalue s = x64_wat(r, off), d = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8, 8), .align = 8 };

	x64_record_emit_memory(e, X64_OP_LD, 8, R11, &s, false);
	x64_record_emit_memory(e, X64_OP_ST, 8, R11, &d, false);
	return (int64_t)d.offset;
}

/* a frame word into a new temporary */
static codegen_value x64_wload(codegen_context *e, const codegen_lvalue *l)
{
	int r = x64_treg(e);

	x64_record_emit_memory(e, X64_OP_LD, 8, r, l, false);
	return x64_wv(r);
}

/* ---------------- arithmetic ---------------- */

VPRIV codegen_value x64_wbin(codegen_context *e, int op, bool sgn, codegen_value a, codegen_value b)
{
	int ra, rb, rd;
	int64_t slot;

	if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) {
		/* inline, as clang (a freestanding vDSO has no libgcc): the words in slots, the count
		   masked to 0..127, three cases (0, below 64, 64 and over) */
		int32_t w0, w1;
		bool shl = op == LEX_TOKEN_LESS_LESS;
		uint32_t lge = walk_label_new(e), lzero = walk_label_new(e), ldone = walk_label_new(e);
		codegen_lvalue L0, L1, LC;
		codegen_value v;
		int r;

		ra = x64_waddr(e, &a);
		w0 = x64_wword(e, ra, 0);
		w1 = x64_wword(e, ra, 8);
		x64_tfree(e, ra);
		v = x64_bin(e, LEX_TOKEN_AMPERSAND, 8, false, b, codegen_value_constant(127));
		r = x64_toreg(e, &v, 8);
		LC = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8, 8), .align = 8 };
		x64_record_emit_memory(e, X64_OP_ST, 8, r, &LC, false);
		x64_tfree(e, r);
		L0 = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = w0, .align = 8 };
		L1 = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = w1, .align = 8 };
		rd = x64_wnew(e, &slot);
		x64_tfree(e, rd);
#define WLD(l) x64_wload(e, &(l))
#define WST(vv, wo_) do { codegen_value t_ = (vv); int rt_ = x64_toreg(e, &t_, 8); codegen_lvalue d_ = { .kind = CODEGEN_LVALUE_FRAME, .offset = slot + (wo_), .align = 8 }; \
		x64_record_emit_memory(e, X64_OP_ST, 8, rt_, &d_, false); x64_tfree(e, rt_); } while (0)
		x64_cmpbr(e, LEX_TOKEN_EQUAL_EQUAL, 8, false, WLD(LC), codegen_value_constant(0), lzero);
		x64_cmpbr(e, LEX_TOKEN_GREATER_EQUAL, 8, false, WLD(LC), codegen_value_constant(64), lge);
		if (shl) {	/* lo << c, hi << c | lo >> (64 - c) */
			WST(x64_bin(e, LEX_TOKEN_LESS_LESS, 8, false, WLD(L0), WLD(LC)), 0);
			WST(x64_bin(e, LEX_TOKEN_PIPE, 8, false, x64_bin(e, LEX_TOKEN_LESS_LESS, 8, false, WLD(L1), WLD(LC)),
				    x64_bin(e, LEX_TOKEN_GREATER_GREATER, 8, false, WLD(L0), x64_bin(e, LEX_TOKEN_MINUS, 8, false, codegen_value_constant(64), WLD(LC)))), 8);
		} else {	/* lo >> c | hi << (64 - c), hi >> c */
			WST(x64_bin(e, LEX_TOKEN_PIPE, 8, false, x64_bin(e, LEX_TOKEN_GREATER_GREATER, 8, false, WLD(L0), WLD(LC)),
				    x64_bin(e, LEX_TOKEN_LESS_LESS, 8, false, WLD(L1), x64_bin(e, LEX_TOKEN_MINUS, 8, false, codegen_value_constant(64), WLD(LC)))), 0);
			WST(x64_bin(e, LEX_TOKEN_GREATER_GREATER, 8, sgn, WLD(L1), WLD(LC)), 8);
		}
		x64_jump(e, ldone);
		x64_label(e, lge);
		if (shl) {
			WST(codegen_value_constant(0), 0);
			WST(x64_bin(e, LEX_TOKEN_LESS_LESS, 8, false, WLD(L0), x64_bin(e, LEX_TOKEN_MINUS, 8, false, WLD(LC), codegen_value_constant(64))), 8);
		} else {
			WST(x64_bin(e, LEX_TOKEN_GREATER_GREATER, 8, sgn, WLD(L1), x64_bin(e, LEX_TOKEN_MINUS, 8, false, WLD(LC), codegen_value_constant(64))), 0);
			WST(sgn ? x64_bin(e, LEX_TOKEN_GREATER_GREATER, 8, true, WLD(L1), codegen_value_constant(63)) : codegen_value_constant(0), 8);
		}
		x64_jump(e, ldone);
		x64_label(e, lzero);
		WST(WLD(L0), 0);
		WST(WLD(L1), 8);
		x64_label(e, ldone);
#undef WLD
#undef WST
		{
			codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = slot, .align = 16 };

			return x64_wv(x64_laddr(e, &l));
		}
	}
	if (0) {
		/* b is an int: __ashlti3, __lshrti3, __ashrti3 (a, (int)b) */
		int64_t w[3];
		int rc;

		ra = x64_waddr(e, &a);
		w[0] = x64_wword(e, ra, 0);
		w[1] = x64_wword(e, ra, 8);
		rc = x64_toreg(e, &b, 8);
		{
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8, 8), .align = 8 };

			x64_record_emit_memory(e, X64_OP_ST, 8, rc, &d, false);
			w[2] = (int64_t)d.offset;
		}
		x64_tfree(e, ra);
		x64_tfree(e, rc);
		rd = x64_wnew(e, &slot);
		x64_lcall(e, op == LEX_TOKEN_LESS_LESS ? CODEGEN_LIBRARY_ASHLTI3 : sgn ? CODEGEN_LIBRARY_ASHRTI3 : CODEGEN_LIBRARY_LSHRTI3, w, 3, -1, slot, 2, false);
		return x64_wv(rd);
	}
	ra = x64_waddr(e, &a);
	rb = x64_waddr(e, &b);
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT) {
		int64_t w[4] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8), x64_wword(e, rb, 0), x64_wword(e, rb, 8) };

		x64_tfree(e, ra);
		x64_tfree(e, rb);
		rd = x64_wnew(e, &slot);
		x64_lcall(e, op == LEX_TOKEN_SLASH ? (sgn ? CODEGEN_LIBRARY_DIVTI3 : CODEGEN_LIBRARY_UDIVTI3) : (sgn ? CODEGEN_LIBRARY_MODTI3 : CODEGEN_LIBRARY_UMODTI3), w, 4, -1, slot, 2, false);
		return x64_wv(rd);
	}
	rd = x64_wnew(e, NULL);
	{
		codegen_lvalue alo = x64_wat(ra, 0), ahi = x64_wat(ra, 8), blo = x64_wat(rb, 0), bhi = x64_wat(rb, 8);
		codegen_lvalue dlo = x64_wat(rd, 0), dhi = x64_wat(rd, 8);
		int t = x64_treg(e);

		if (op == LEX_TOKEN_STAR) {
			/*
			 * hi = alo * bhi + ahi * blo + high(alo * blo), lo = low(alo * blo).  Everything
			 * through frame slots: mul writes rdx:rax, and any operand register may be one
			 * of them.
			 */
			static const uint8_t rr[2] = { RAX, RDX };
			int64_t w[4] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8), x64_wword(e, rb, 0), x64_wword(e, rb, 8) }, ds;
			codegen_lvalue s0 = { .kind = CODEGEN_LVALUE_FRAME, .offset = w[0] }, s1 = { .kind = CODEGEN_LVALUE_FRAME, .offset = w[1] };
			codegen_lvalue s2 = { .kind = CODEGEN_LVALUE_FRAME, .offset = w[2] }, s3 = { .kind = CODEGEN_LVALUE_FRAME, .offset = w[3] };
			codegen_lvalue flo, fhi;
			uint32_t sv;

			x64_tfree(e, rd);
			rd = x64_wnew(e, &ds);
			flo = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = ds }, fhi = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = ds + 8 };
			x64_record_emit_memory(e, X64_OP_LD, 8, t, &s0, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &s3, false);
			x64_record_emit_three_registers(e, X64_OP_IMUL, 8, t, t, R11);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &s1, false);
			x64_record_emit_memory(e, X64_OP_ST, 8, t, &fhi, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, t, &s2, false);
			x64_record_emit_three_registers(e, X64_OP_IMUL, 8, R11, R11, t);
			x64_record_emit_memory(e, X64_OP_LD, 8, t, &fhi, false);
			x64_record_emit_three_registers(e, X64_OP_ADD, 8, t, t, R11);
			x64_record_emit_memory(e, X64_OP_ST, 8, t, &fhi, false);
			sv = x64_save(e, rr, 2, -1, -1);
			x64_record_emit_memory(e, X64_OP_LD, 8, RAX, &s0, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &s2, false);
			x64_record_emit_three_registers(e, X64_OP_MULW, 8, X64_REGISTER_NONE, R11, X64_REGISTER_NONE);
			x64_record_emit_memory(e, X64_OP_ST, 8, RAX, &flo, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &fhi, false);
			x64_record_emit_three_registers(e, X64_OP_ADD, 8, R11, R11, RDX);
			x64_record_emit_memory(e, X64_OP_ST, 8, R11, &fhi, false);
			x64_restore(e, sv);
		} else {
			static const int lo_op[] = { [LEX_TOKEN_PLUS] = X64_OP_ADD, [LEX_TOKEN_MINUS] = X64_OP_SUB, [LEX_TOKEN_AMPERSAND] = X64_OP_AND, [LEX_TOKEN_PIPE] = X64_OP_OR, [LEX_TOKEN_CARET] = X64_OP_XOR };
			int xo, xhi;

			if (op != LEX_TOKEN_PLUS && op != LEX_TOKEN_MINUS && op != LEX_TOKEN_AMPERSAND && op != LEX_TOKEN_PIPE && op != LEX_TOKEN_CARET)
				codegen_fatal_report(e, e->current_location, "x86-64: __int128 operator %d", op);
			xo = lo_op[op];
			xhi = op == LEX_TOKEN_PLUS ? X64_OP_ADC : op == LEX_TOKEN_MINUS ? X64_OP_SBB : xo;
			x64_record_emit_memory(e, X64_OP_LD, 8, t, &alo, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &blo, false);
			x64_record_emit_three_registers(e, xo, 8, t, t, R11);
			x64_record_emit_memory(e, X64_OP_ST, 8, t, &dlo, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, t, &ahi, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bhi, false);
			x64_record_emit_three_registers(e, xhi, 8, t, t, R11);	/* (mov does not touch the carry) */
			x64_record_emit_memory(e, X64_OP_ST, 8, t, &dhi, false);
		}
		x64_tfree(e, t);
	}
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	return x64_wv(rd);
}

VPRIV codegen_value x64_wun(codegen_context *e, int op, codegen_value a)
{
	int ra = x64_waddr(e, &a), rd = x64_wnew(e, NULL), t = x64_treg(e);
	codegen_lvalue alo = x64_wat(ra, 0), ahi = x64_wat(ra, 8), dlo = x64_wat(rd, 0), dhi = x64_wat(rd, 8);

	if (op == LEX_TOKEN_TILDE) {
		x64_record_emit_memory(e, X64_OP_LD, 8, t, &alo, false);
		x64_record_emit_three_registers(e, X64_OP_NOT, 8, t, t, X64_REGISTER_NONE);
		x64_record_emit_memory(e, X64_OP_ST, 8, t, &dlo, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, t, &ahi, false);
		x64_record_emit_three_registers(e, X64_OP_NOT, 8, t, t, X64_REGISTER_NONE);
		x64_record_emit_memory(e, X64_OP_ST, 8, t, &dhi, false);
	} else {
		/* 0 - a */
		x64_record_emit_move_immediate(e, t, 0);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &alo, false);
		x64_record_emit_three_registers(e, X64_OP_SUB, 8, t, t, R11);
		x64_record_emit_memory(e, X64_OP_ST, 8, t, &dlo, false);
		x64_record_emit_move_immediate(e, t, 0);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &ahi, false);
		x64_record_emit_three_registers(e, X64_OP_SBB, 8, t, t, R11);
		x64_record_emit_memory(e, X64_OP_ST, 8, t, &dhi, false);
	}
	x64_tfree(e, t);
	x64_tfree(e, ra);
	return x64_wv(rd);
}

/* a op b as 0 or 1 */
VPRIV codegen_value x64_wcmp(codegen_context *e, int op, bool sgn, codegen_value a, codegen_value b)
{
	int ra = x64_waddr(e, &a), rb = x64_waddr(e, &b), t = x64_treg(e);
	bool swap = op == LEX_TOKEN_GREATER || op == LEX_TOKEN_LESS_EQUAL;
	int pa = swap ? rb : ra, pb = swap ? ra : rb, cc;
	codegen_lvalue alo = x64_wat(pa, 0), ahi = x64_wat(pa, 8), blo = x64_wat(pb, 0), bhi = x64_wat(pb, 8);

	if (op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL) {
		int u = x64_treg(e);

		x64_record_emit_memory(e, X64_OP_LD, 8, t, &alo, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &blo, false);
		x64_record_emit_three_registers(e, X64_OP_XOR, 8, t, t, R11);
		x64_record_emit_memory(e, X64_OP_LD, 8, u, &ahi, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bhi, false);
		x64_record_emit_three_registers(e, X64_OP_XOR, 8, u, u, R11);
		x64_record_emit_three_registers(e, X64_OP_OR, 8, t, t, u);
		x64_tfree(e, u);
		cc = op == LEX_TOKEN_EQUAL_EQUAL ? X64_CONDITION_E : X64_CONDITION_NE;
	} else {
		/* x < y as the flags of the 128-bit x - y: cmp the low words, sbb the high ones */
		x64_record_emit_memory(e, X64_OP_LD, 8, t, &alo, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &blo, false);
		x64_record_emit_three_registers(e, X64_OP_CMP, 8, X64_REGISTER_NONE, t, R11);
		x64_record_emit_memory(e, X64_OP_LD, 8, t, &ahi, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bhi, false);
		x64_record_emit_three_registers(e, X64_OP_SBB, 8, t, t, R11);
		/* <: x=a, y=b; >: x=b, y=a; >=: !(a < b); <=: !(b < a) */
		cc = op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ? (sgn ? X64_CONDITION_L : X64_CONDITION_B) : (sgn ? X64_CONDITION_GE : X64_CONDITION_AE);
	}
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	x64_record_emit_three_registers(e, X64_OP_SETCC, 4, t, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = (uint8_t)cc;
	return x64_wv(t);
}

/* ---------------- conversions ---------------- */

/* v of type from as type to, __int128 on at least one side */
VPRIV codegen_value x64_wcvt(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;
	bool fw = KIND(c, from) == AST_TYPE_INT128 || KIND(c, from) == AST_TYPE_UINT128;
	bool tw = KIND(c, to) == AST_TYPE_INT128 || KIND(c, to) == AST_TYPE_UINT128;
	int64_t slot;

	if (fw && tw)
		return v;
	if (KIND(c, from) == AST_TYPE_FLOAT128 || KIND(c, to) == AST_TYPE_FLOAT128)
		return x64_cvt(e, v, from, to);	/* (fp.c: libgcc's __fixtfti, __floattitf ...) */
	if (KIND(c, from) == AST_TYPE_LONG_DOUBLE || KIND(c, to) == AST_TYPE_LONG_DOUBLE) {
		/* x86 long double uses the x87 helpers, not binary128's TF ABI.
		 * Both source representations occupy a 16-byte object; let the
		 * normal call classifier place it on the stack or in integer regs. */
		walk_argument arg, result;
		int ra = x64_waddr(e, &v);
		int fn = tw ? (walk_type_is_signed(c, to) ? CODEGEN_LIBRARY_FIXXFTI : CODEGEN_LIBRARY_FIXUNSXFTI) :
			(walk_type_is_signed(c, from) ? CODEGEN_LIBRARY_FLOATTIXF : CODEGEN_LIBRARY_FLOATUNTIXF);

		x64_classify(e, from, &arg, false);
		arg.slot = walk_slot_temporary(e, 16, 16);
		for (int w = 0; w < 2; w++) {
			codegen_lvalue src = x64_wat(ra, 8 * w);
			codegen_lvalue dst = { .kind = CODEGEN_LVALUE_FRAME, .offset = arg.slot + 8 * w, .align = 8 };

			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &src, false);
			x64_record_emit_memory(e, X64_OP_ST, 8, R11, &dst, false);
		}
		x64_tfree(e, ra);
		x64_classify(e, to, &result, false);
		result.slot = walk_slot_temporary(e, 16, 16);
		x64_call(e, CODEGEN_FIXED_REFERENCE(fn), NULL, &arg, 1, false, &result, to);
		codegen_lvalue dst = { .kind = CODEGEN_LVALUE_FRAME, .offset = result.slot, .align = 16 };
		return x64_wv(x64_laddr(e, &dst));
	}
	if (tw && v.kind == CODEGEN_VALUE_CONSTANT)
		return x64_wconst(e, walk_type_is_signed(c, from) ? v.integer : (i128)(u128)(uint64_t)v.integer);
	if (tw && codegen_type_is_float(c, from)) {
		int fs = walk_type_size(c, from), rd;
		int64_t w[1];
		codegen_lvalue d = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8, 8), .align = 8 };

		x64_record_emit_memory(e, X64_OP_ST, 8, v.register_number, &d, false);
		x64_tfree(e, v.register_number);
		w[0] = (int64_t)d.offset;
		rd = x64_wnew(e, &slot);
		x64_lcall(e, KIND(c, to) == AST_TYPE_INT128 ? (fs == 4 ? CODEGEN_LIBRARY_FIXSFTI : CODEGEN_LIBRARY_FIXDFTI) : (fs == 4 ? CODEGEN_LIBRARY_FIXUNSSFTI : CODEGEN_LIBRARY_FIXUNSDFTI),
			  w, 1, 0, slot, 2, false);
		return x64_wv(rd);
	}
	if (tw) {
		/* an integer: its value extended to 64 bits, the high word its sign or 0 */
		int r = x64_toreg(e, &v, 8), rd = x64_wnew(e, NULL);
		codegen_lvalue lo = x64_wat(rd, 0), hi = x64_wat(rd, 8);

		if (walk_type_size(c, from) < 8)
			x64_ext(e, r, walk_type_size(c, from), walk_type_is_signed(c, from));
		x64_record_emit_memory(e, X64_OP_ST, 8, r, &lo, false);
		if (walk_type_is_signed(c, from)) {
			x64_record_emit_three_registers(e, X64_OP_SARI, 8, r, r, X64_REGISTER_NONE)->immediate = 63;
		} else {
			x64_record_emit_move_immediate(e, r, 0);
		}
		x64_record_emit_memory(e, X64_OP_ST, 8, r, &hi, false);
		x64_tfree(e, r);
		return x64_wv(rd);
	}
	if (KIND(c, to) == AST_TYPE_BOOL) {
		int ra = x64_waddr(e, &v), t = x64_treg(e);
		codegen_lvalue lo = x64_wat(ra, 0), hi = x64_wat(ra, 8);

		x64_record_emit_memory(e, X64_OP_LD, 8, t, &lo, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &hi, false);
		x64_record_emit_three_registers(e, X64_OP_OR, 8, t, t, R11);
		x64_tfree(e, ra);
		x64_record_emit_three_registers(e, X64_OP_SETCC, 4, t, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = X64_CONDITION_NE;
		return x64_wv(t);
	}
	if (codegen_type_is_float(c, to)) {
		int ra = x64_waddr(e, &v), ts = walk_type_size(c, to), x;
		int64_t w[2] = { x64_wword(e, ra, 0), x64_wword(e, ra, 8) };
		codegen_lvalue d = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8, 8), .align = 8 };

		x64_tfree(e, ra);
		x64_lcall(e, KIND(c, from) == AST_TYPE_INT128 ? (ts == 4 ? CODEGEN_LIBRARY_FLOATTISF : CODEGEN_LIBRARY_FLOATTIDF) : (ts == 4 ? CODEGEN_LIBRARY_FLOATUNTISF : CODEGEN_LIBRARY_FLOATUNTIDF),
			  w, 2, -1, (int64_t)d.offset, 1, true);
		x = x64_ftreg(e);
		x64_record_emit_memory(e, X64_OP_LD, ts, x, &d, false);
		return x64_wv(x);
	}
	{
		/* to a narrower integer: the low word */
		int ra = x64_waddr(e, &v), r = x64_treg(e);
		codegen_lvalue lo = x64_wat(ra, 0);
		int ts = walk_type_size(c, to);

		x64_record_emit_memory(e, X64_OP_LD, 8, r, &lo, false);
		x64_tfree(e, ra);
		if (ts < 4)
			x64_ext(e, r, ts, walk_type_is_signed(c, to));
		return x64_wv(r);
	}
}

/* ---------------- arbitrary-width integer objects ---------------- */

static int x64_bignew(codegen_context *e, ast_qualified_type t)
{
	codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, codegen_type_size(e->compile_context, t), 8), .align = 8 };
	return x64_laddr(e, &l);
}

static void x64_bignorm(codegen_context *e, int rd, ast_qualified_type t)
{
	cc *c = e->compile_context;
	unsigned bits = T(c, t)->n, rem = bits & 63;
	if (rem) {
		codegen_lvalue top = x64_wat(rd, (int)codegen_type_size(c, t) - 8);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &top, false);
		x64_record_emit_three_registers(e, X64_OP_SHLI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 64 - rem;
		x64_record_emit_three_registers(e, walk_type_is_signed(c, t) ? X64_OP_SARI : X64_OP_SHRI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 64 - rem;
		x64_record_emit_memory(e, X64_OP_ST, 8, R11, &top, false);
	}
}

VPRIV codegen_value x64_bigconst(codegen_context *e, ast_qualified_type t, const uint32_t *words, i128 small)
{
	int rd = x64_bignew(e, t);
	unsigned n = (unsigned)codegen_type_size(e->compile_context, t) / 8;
	for (unsigned i = 0; i < n; i++) {
		codegen_lvalue l = x64_wat(rd, (int)i * 8);
		uint64_t w = words ? (uint64_t)words[2*i] | (uint64_t)words[2*i+1] << 32 :
			i < 2 ? (uint64_t)((u128)small >> (i * 64)) : small < 0 ? UINT64_MAX : 0;
		x64_record_emit_move_immediate(e, R11, w);
		x64_record_emit_memory(e, X64_OP_ST, 8, R11, &l, false);
	}
	x64_bignorm(e, rd, t);
	return x64_wv(rd);
}

static codegen_value x64_bigslot(codegen_context *e, int64_t slot)
{
	codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = slot, .align = 8 };
	int r = x64_treg(e);
	x64_record_emit_memory(e, X64_OP_LD, 8, r, &l, false);
	return x64_wv(r);
}

/* Return a limb at i +/- whole, extending beyond either end without
 * forming an out-of-bounds load.  A negative index fails the unsigned bound. */
static codegen_value x64_bigword(codegen_context *e, int ra, unsigned n, int i, bool right, bool sgn, int64_t whole)
{
	codegen_value index = x64_bin(e, right ? LEX_TOKEN_PLUS : LEX_TOKEN_MINUS, 8, false, codegen_value_constant(i), x64_bigslot(e, whole));
	int ri = x64_toreg(e, &index, 8), r = x64_treg(e), cmp = x64_treg(e);
	uint32_t done = walk_label_new(e);
	if (right && sgn) {
		codegen_lvalue top = x64_wat(ra, (int)(n-1)*8);
		x64_record_emit_memory(e, X64_OP_LD, 8, r, &top, false);
		x64_record_emit_three_registers(e, X64_OP_SARI, 8, r, r, X64_REGISTER_NONE)->immediate = 63;
	} else x64_record_emit_move_immediate(e, r, 0);
	x64_mov(e, cmp, ri);
	x64_cmpbr(e, LEX_TOKEN_GREATER_EQUAL, 8, false, x64_wv(cmp), codegen_value_constant(n), done);
	x64_record_emit_three_registers(e, X64_OP_SHLI, 8, ri, ri, X64_REGISTER_NONE)->immediate = 3;
	x64_record_emit_three_registers(e, X64_OP_ADD, 8, ri, ri, ra);
	codegen_lvalue src = x64_wat(ri, 0);
	x64_record_emit_memory(e, X64_OP_LD, 8, r, &src, false);
	x64_label(e, done);
	x64_tfree(e, ri);
	return x64_wv(r);
}

static codegen_value x64_bigshift(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	bool right = op == LEX_TOKEN_GREATER_GREATER;
	unsigned n = (unsigned)codegen_type_size(e->compile_context, t)/8;
	int ra = x64_toreg(e, &a, 8), rd = x64_bignew(e, t);
	int64_t whole = walk_slot_temporary(e, 8, 8), bits = walk_slot_temporary(e, 8, 8);
	codegen_lvalue wl = { .kind = CODEGEN_LVALUE_FRAME, .offset = whole, .align = 8 }, bl = wl;
	bl.offset = bits;
	int rb = x64_toreg(e, &b, 8), r = x64_treg(e);
	x64_record_emit_three_registers(e, X64_OP_SHRI, 8, r, rb, X64_REGISTER_NONE)->immediate = 6;
	x64_record_emit_memory(e, X64_OP_ST, 8, r, &wl, false);
	x64_tfree(e, r);
	b = x64_bin(e, LEX_TOKEN_AMPERSAND, 8, false, b, codegen_value_constant(63));
	x64_record_emit_memory(e, X64_OP_ST, 8, b.register_number, &bl, false);
	x64_tfree(e, b.register_number);
	for (unsigned i = 0; i < n; i++) {
		codegen_lvalue out = x64_wat(rd, (int)i*8);
		uint32_t done = walk_label_new(e);
		codegen_value v = x64_bigword(e, ra, n, (int)i, right, right && walk_type_is_signed(e->compile_context, t), whole);
		v = x64_bin(e, op, 8, false, v, x64_bigslot(e, bits));
		x64_record_emit_memory(e, X64_OP_ST, 8, v.register_number, &out, false);
		x64_tfree(e, v.register_number);
		x64_brz(e, x64_bigslot(e, bits), 8, false, done);
		v = x64_bigword(e, ra, n, (int)i + (right ? 1 : -1), right, right && walk_type_is_signed(e->compile_context, t), whole);
		codegen_value count = x64_bin(e, LEX_TOKEN_MINUS, 8, false, codegen_value_constant(64), x64_bigslot(e, bits));
		v = x64_bin(e, right ? LEX_TOKEN_LESS_LESS : LEX_TOKEN_GREATER_GREATER, 8, false, v, count);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &out, false);
		x64_record_emit_three_registers(e, X64_OP_OR, 8, v.register_number, v.register_number, R11);
		x64_record_emit_memory(e, X64_OP_ST, 8, v.register_number, &out, false);
		x64_tfree(e, v.register_number);
		x64_label(e, done);
	}
	x64_tfree(e, ra);
	x64_bignorm(e, rd, t);
	return x64_wv(rd);
}

/* Conditional two's-complement negation of a private limb object. */
static void x64_bigneg_if(codegen_context *e, int ptr, unsigned n, codegen_value sign)
{
	uint32_t done = walk_label_new(e);
	x64_brz(e, sign, 8, false, done);
	int r = x64_treg(e);
	for (unsigned i = 0; i < n; i++) {
		codegen_lvalue l = x64_wat(ptr, (int)i*8);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &l, false);
		x64_record_emit_move_immediate(e, r, 0);
		x64_record_emit_three_registers(e, i ? X64_OP_SBB : X64_OP_SUB, 8, r, r, R11);
		x64_record_emit_memory(e, X64_OP_ST, 8, r, &l, false);
	}
	x64_tfree(e, r);
	x64_label(e, done);
}

/* Restoring division: Q initially holds the dividend. Each iteration moves
 * its high bit into the remainder and appends one quotient bit. The extra
 * remainder limb retains carry even for a divisor with its top bit set. */
static codegen_value x64_bigdiv(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	unsigned n = (unsigned)codegen_type_size(e->compile_context, t)/8;
	int qa = x64_toreg(e, &a, 8), db = x64_toreg(e, &b, 8);
	int q = x64_bignew(e, t), d = x64_bignew(e, t);
	x64_copy(e, q, qa, 8*n);
	x64_copy(e, d, db, 8*n);
	x64_tfree(e, qa);
	x64_tfree(e, db);
	codegen_lvalue rl = { .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, 8*(n+1), 8), .align = 8 };
	int rem = x64_laddr(e, &rl);
	x64_zero(e, rem, 8*(n+1));
	int64_t sa = walk_slot_temporary(e, 8, 8), sb = walk_slot_temporary(e, 8, 8);
	if (walk_type_is_signed(e->compile_context, t)) {
		codegen_lvalue la = { .kind = CODEGEN_LVALUE_FRAME, .offset = sa, .align = 8 }, lb = la;
		lb.offset = sb;
		codegen_lvalue at = x64_wat(q, (int)(n-1)*8), bt = x64_wat(d, (int)(n-1)*8);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &at, false);
		x64_record_emit_three_registers(e, X64_OP_SARI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 63;
		x64_record_emit_memory(e, X64_OP_ST, 8, R11, &la, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bt, false);
		x64_record_emit_three_registers(e, X64_OP_SARI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 63;
		x64_record_emit_memory(e, X64_OP_ST, 8, R11, &lb, false);
		x64_bigneg_if(e, q, n, x64_bigslot(e, sa));
		x64_bigneg_if(e, d, n, x64_bigslot(e, sb));
	}
	int r = x64_treg(e), count = x64_treg(e);
	uint32_t loop = walk_label_new(e), less = walk_label_new(e);
	x64_record_emit_move_immediate(e, count, n*64);
	x64_label(e, loop);
	for (unsigned i = 0; i < n; i++) {
		codegen_lvalue l = x64_wat(q, (int)i*8);
		x64_record_emit_memory(e, X64_OP_LD, 8, r, &l, false);
		x64_record_emit_three_registers(e, i ? X64_OP_ADC : X64_OP_ADD, 8, r, r, r);
		x64_record_emit_memory(e, X64_OP_ST, 8, r, &l, false);
	}
	for (unsigned i = 0; i <= n; i++) {
		codegen_lvalue l = x64_wat(rem, (int)i*8);
		x64_record_emit_memory(e, X64_OP_LD, 8, r, &l, false);
		x64_record_emit_three_registers(e, X64_OP_ADC, 8, r, r, r);
		x64_record_emit_memory(e, X64_OP_ST, 8, r, &l, false);
	}
	for (unsigned i = 0; i <= n; i++) {
		codegen_lvalue al = x64_wat(rem, (int)i*8), bl = x64_wat(d, (int)i*8);
		x64_record_emit_memory(e, X64_OP_LD, 8, r, &al, false);
		if (i < n) x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bl, false);
		else x64_record_emit_move_immediate(e, R11, 0);
		x64_record_emit_three_registers(e, i ? X64_OP_SBB : X64_OP_SUB, 8, r, r, R11);
	}
	record *branch = x64_record_emit(e, X64_OP_JCC);
	branch->condition = X64_CONDITION_B;
	branch->label = less;
	for (unsigned i = 0; i <= n; i++) {
		codegen_lvalue al = x64_wat(rem, (int)i*8), bl = x64_wat(d, (int)i*8);
		x64_record_emit_memory(e, X64_OP_LD, 8, r, &al, false);
		if (i < n) x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bl, false);
		else x64_record_emit_move_immediate(e, R11, 0);
		x64_record_emit_three_registers(e, i ? X64_OP_SBB : X64_OP_SUB, 8, r, r, R11);
		x64_record_emit_memory(e, X64_OP_ST, 8, r, &al, false);
	}
	codegen_lvalue qlo = x64_wat(q, 0);
	x64_record_emit_memory(e, X64_OP_LD, 8, r, &qlo, false);
	x64_record_emit_three_registers(e, X64_OP_ORI, 8, r, r, X64_REGISTER_NONE)->immediate = 1;
	x64_record_emit_memory(e, X64_OP_ST, 8, r, &qlo, false);
	x64_label(e, less);
	x64_record_emit_three_registers(e, X64_OP_SUBI, 8, count, count, X64_REGISTER_NONE)->immediate = 1;
	branch = x64_record_emit(e, X64_OP_JCC);
	branch->condition = X64_CONDITION_NE;
	branch->label = loop;
	x64_tfree(e, count);
	x64_tfree(e, r);
	x64_tfree(e, d);
	int result = op == LEX_TOKEN_SLASH ? q : rem;
	x64_tfree(e, op == LEX_TOKEN_SLASH ? rem : q);
	if (walk_type_is_signed(e->compile_context, t)) {
		codegen_value sign = x64_bigslot(e, sa);
		if (op == LEX_TOKEN_SLASH) sign = x64_bin(e, LEX_TOKEN_CARET, 8, false, sign, x64_bigslot(e, sb));
		x64_bigneg_if(e, result, n, sign);
	}
	x64_bignorm(e, result, t);
	return x64_wv(result);
}

VPRIV codegen_value x64_bigbin(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	if (a.kind == CODEGEN_VALUE_CONSTANT) a = x64_bigconst(e, t, NULL, a.integer);
	if (b.kind == CODEGEN_VALUE_CONSTANT && op != LEX_TOKEN_LESS_LESS && op != LEX_TOKEN_GREATER_GREATER)
		b = x64_bigconst(e, t, NULL, b.integer);
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT)
		return x64_bigdiv(e, op, t, a, b);
	if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER)
		return x64_bigshift(e, op, t, a, b);
	cc *c = e->compile_context;
	unsigned n = (unsigned)codegen_type_size(c, t) / 8;
	int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8);
	int rd = x64_bignew(e, t), r = x64_treg(e);
	if (op == LEX_TOKEN_STAR) {
		int carry = x64_treg(e);
		x64_zero(e, rd, codegen_type_size(c, t));
		for (unsigned i = 0; i < 2*n; i++) {
			x64_record_emit_move_immediate(e, carry, 0);
			for (unsigned j = 0; j < 2*n-i; j++) {
				codegen_lvalue al = x64_wat(ra, (int)i * 4), bl = x64_wat(rb, (int)j * 4);
				codegen_lvalue dl = x64_wat(rd, (int)(i+j) * 4);
				x64_record_emit_memory(e, X64_OP_LD, 4, r, &al, false);
				x64_record_emit_memory(e, X64_OP_LD, 4, R11, &bl, false);
				x64_record_emit_three_registers(e, X64_OP_IMUL, 8, r, r, R11);
				x64_record_emit_memory(e, X64_OP_LD, 4, R11, &dl, false);
				x64_record_emit_three_registers(e, X64_OP_ADD, 8, r, r, R11);
				x64_record_emit_three_registers(e, X64_OP_ADD, 8, r, r, carry);
				x64_record_emit_memory(e, X64_OP_ST, 4, r, &dl, false);
				x64_record_emit_three_registers(e, X64_OP_SHRI, 8, carry, r, X64_REGISTER_NONE)->immediate = 32;
			}
		}
		x64_tfree(e, carry);
	} else {
		int first = op == LEX_TOKEN_PLUS ? X64_OP_ADD : op == LEX_TOKEN_MINUS ? X64_OP_SUB :
			op == LEX_TOKEN_AMPERSAND ? X64_OP_AND : op == LEX_TOKEN_PIPE ? X64_OP_OR : op == LEX_TOKEN_CARET ? X64_OP_XOR : 0;
		int next = op == LEX_TOKEN_PLUS ? X64_OP_ADC : op == LEX_TOKEN_MINUS ? X64_OP_SBB : first;
		if (!first) codegen_fatal_report(e, 0, "wide _BitInt operation not implemented");
		for (unsigned i = 0; i < n; i++) {
			codegen_lvalue al = x64_wat(ra, (int)i * 8), bl = x64_wat(rb, (int)i * 8), dl = x64_wat(rd, (int)i * 8);
			x64_record_emit_memory(e, X64_OP_LD, 8, r, &al, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bl, false);
			x64_record_emit_three_registers(e, i ? next : first, 8, r, r, R11);
			x64_record_emit_memory(e, X64_OP_ST, 8, r, &dl, false);
		}
	}
	x64_tfree(e, r);
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	x64_bignorm(e, rd, t);
	return x64_wv(rd);
}

VPRIV codegen_value x64_bigcmp(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	if (a.kind == CODEGEN_VALUE_CONSTANT) a = x64_bigconst(e, t, NULL, a.integer);
	if (b.kind == CODEGEN_VALUE_CONSTANT) b = x64_bigconst(e, t, NULL, b.integer);
	unsigned n = (unsigned)codegen_type_size(e->compile_context, t) / 8;
	int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8), r = x64_treg(e), cc;
	bool eq = op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL;
	bool swap = op == LEX_TOKEN_GREATER || op == LEX_TOKEN_LESS_EQUAL;
	int u = eq ? x64_treg(e) : -1;
	if (eq) x64_record_emit_move_immediate(e, r, 0);
	for (unsigned i = 0; i < n; i++) {
		codegen_lvalue al = x64_wat(swap ? rb : ra, (int)i * 8), bl = x64_wat(swap ? ra : rb, (int)i * 8);
		x64_record_emit_memory(e, X64_OP_LD, 8, eq ? u : r, &al, false);
		x64_record_emit_memory(e, X64_OP_LD, 8, R11, &bl, false);
		x64_record_emit_three_registers(e, eq ? X64_OP_XOR : i ? X64_OP_SBB : X64_OP_SUB, 8, eq ? u : r, eq ? u : r, R11);
		if (eq) x64_record_emit_three_registers(e, X64_OP_OR, 8, r, r, u);
	}
	if (eq) {
		x64_tfree(e, u);
		cc = op == LEX_TOKEN_EQUAL_EQUAL ? X64_CONDITION_E : X64_CONDITION_NE;
	} else {
		bool s = walk_type_is_signed(e->compile_context, t);
		cc = op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ? (s ? X64_CONDITION_L : X64_CONDITION_B) : (s ? X64_CONDITION_GE : X64_CONDITION_AE);
	}
	x64_tfree(e, ra);
	x64_tfree(e, rb);
	x64_record_emit_three_registers(e, X64_OP_SETCC, 4, r, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = (uint8_t)cc;
	return x64_wv(r);
}

VPRIV codegen_value x64_bigcvt(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;
	bool fb = codegen_type_is_bitint_big(c, from), tb = codegen_type_is_bitint_big(c, to);
	if (KIND(c, to) == AST_TYPE_VOID) {
		walk_value_free(e, &v);
		return codegen_value_constant(0);
	}
	if (codegen_type_is_float(c, from) || codegen_type_is_float(c, to))
		codegen_fatal_report(e, 0, "wide _BitInt floating conversion not implemented");
	if (tb) {
		if (!fb && !((KIND(c, from) == AST_TYPE_INT128) || (KIND(c, from) == AST_TYPE_UINT128)))
			v = x64_wcvt(e, v, from, codegen_type_primitive(AST_TYPE_INT128));
		unsigned src = fb ? (unsigned)codegen_type_size(c, from) / 8 : 2, dst = (unsigned)codegen_type_size(c, to) / 8;
		int rs = x64_waddr(e, &v), rd = x64_bignew(e, to);
		for (unsigned i = 0; i < dst; i++) {
			codegen_lvalue d = x64_wat(rd, (int)i * 8);
			if (i < src) {
				codegen_lvalue s = x64_wat(rs, (int)i * 8);
				x64_record_emit_memory(e, X64_OP_LD, 8, R11, &s, false);
			} else if (i == src) {
				if (walk_type_is_signed(c, from)) x64_record_emit_three_registers(e, X64_OP_SARI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 63;
				else x64_record_emit_move_immediate(e, R11, 0);
			}
			x64_record_emit_memory(e, X64_OP_ST, 8, R11, &d, false);
		}
		x64_tfree(e, rs);
		x64_bignorm(e, rd, to);
		return x64_wv(rd);
	}
	int rs = x64_toreg(e, &v, 8), rd;
	if (KIND(c, to) == AST_TYPE_BOOL) {
		rd = x64_treg(e);
		x64_record_emit_move_immediate(e, rd, 0);
		for (unsigned i = 0; i < codegen_type_size(c, from)/8; i++) {
			codegen_lvalue l = x64_wat(rs, (int)i*8);
			x64_record_emit_memory(e, X64_OP_LD, 8, R11, &l, false);
			x64_record_emit_three_registers(e, X64_OP_OR, 8, rd, rd, R11);
		}
		x64_record_emit_three_registers(e, X64_OP_SETCC, 4, rd, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = X64_CONDITION_NE;
	} else if (codegen_type_size(c, to) > 8) {
		rd = x64_wnew(e, NULL);
		x64_copy(e, rd, rs, 16);
	} else {
		codegen_lvalue l = x64_wat(rs, 0);
		rd = x64_treg(e);
		x64_record_emit_memory(e, X64_OP_LD, (int)codegen_type_size(c, to), rd, &l, walk_type_is_signed(c, to));
	}
	x64_tfree(e, rs);
	return x64_wv(rd);
}
