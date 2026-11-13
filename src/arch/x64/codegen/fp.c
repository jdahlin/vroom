/* vcc code generator, x86-64: floating point (x64-internal.h).  float and double
 * in SSE registers (scalar ss/sd); long double on the x87 stack, its values
 * in 16-byte frame slots (the walker holds their addresses, as for records). */
#include "x64-internal.h"

VPRIV bool x64_ldmem(void) { return true; }

static bool x64_isld(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_LONG_DOUBLE; }
static bool x64_isq(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_FLOAT128; }
static int x64_fsz(cc *c, ast_qualified_type t) { return KIND(c, t) == AST_TYPE_FLOAT ? 4 : 8; }
static codegen_value x64_vr(int r) { return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER, .register_number = (uint8_t)r }; }

/* an x87 instruction, with its memory operand l (NULL: none) */
VPRIV void x64_x87(codegen_context *e, int xf, const codegen_lvalue *l, int sz)
{
	x64_nofp(e);
	if (l && (l->offset < INT32_MIN || l->offset > INT32_MAX)) {
		uint32_t live = e->x64_context->live;
		if (l->kind == CODEGEN_LVALUE_REGISTER && l->register_number < 32) e->x64_context->live |= 1u << l->register_number;
		int address = x64_treg(e);
		x64_record_emit_memory(e, X64_OP_LEA, 8, address, l, false);
		codegen_lvalue at = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)address };
		x64_x87(e, xf, &at, sz);
		e->x64_context->live = live;
		return;
	}
	uint32_t ref = 0;
	int base = l ? x64_memory_base_register(e, l, &ref) : 0;
	record *i = x64_record_emit(e, X64_OP_X87);

	i->condition = (uint8_t)xf;
	i->size = (uint8_t)sz;
	if (l) {
		i->offset = (int32_t)l->offset;
		i->registers[1] = (uint8_t)base;
		i->reference = ref;
	}
}

VPRIV void x64_x87ld(codegen_context *e, const codegen_lvalue *l) { x64_x87(e, X64_X87_LDT, l, 10); }
VPRIV void x64_x87st(codegen_context *e, const codegen_lvalue *l) { x64_x87(e, X64_X87_STPT, l, 10); }

static codegen_lvalue x64_at(int r) { return (codegen_lvalue){ .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)r, .align = 16 }; }
static codegen_lvalue x64_tmpslot(codegen_context *e, uint64_t n) { return (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = walk_slot_temporary(e, n, n >= 16 ? 16 : 8), .align = 8 }; }

/* a new long double object: its address in a temporary */
static int x64_ldnew(codegen_context *e)
{
	codegen_lvalue l = x64_tmpslot(e, 16);

	return x64_laddr(e, &l);
}

static void x64_jcc(codegen_context *e, int cc, uint32_t l)
{
	record *i = x64_record_emit(e, X64_OP_JCC);

	i->condition = (uint8_t)cc;
	i->label = l;
}

static codegen_value x64_qbin(codegen_context *e, int k, codegen_value a, codegen_value b);
static codegen_value x64_qcmp(codegen_context *e, int op, codegen_value a, codegen_value b);
static codegen_value x64_qcvt(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);
static codegen_value x64_qzero(codegen_context *e);
/* Compiler-runtime conversion helpers take/return half bits in xmm0.
   Use the regular ABI call path so every live temporary survives. */
static codegen_value x64_half_call(codegen_context *e, int name, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	walk_argument a;
	codegen_lvalue slot = x64_tmpslot(e, 16);

	x64_classify(e, from, &a, false);
	a.slot = slot.offset;
	if (x64_isld(e->compile_context, from)) {
		codegen_lvalue src = x64_at(x64_toreg(e, &v, 8));

		x64_x87ld(e, &src);
		x64_x87st(e, &slot);
	} else
		x64_store(e, v.register_number, &slot, walk_type_size(e->compile_context, from));
	x64_tfree(e, v.register_number);
	return x64_call(e, CODEGEN_FIXED_REFERENCE(name), NULL, &a, 1, false, NULL, to);
}

VPRIV codegen_value x64_fconst(codegen_context *e, uint64_t bits, int size)
{
	x64_nofp(e);
	int r = x64_ftreg(e);

	x64_record_emit_move_immediate(e, R11, (int64_t)bits);
	x64_record_emit_three_registers(e, X64_OP_MOV, size <= 4 ? 4 : 8, r, R11, X64_REGISTER_NONE);
	return x64_vr(r);
}

VPRIV codegen_value x64_fbin(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	cc *c = e->compile_context;
	int k = op == LEX_TOKEN_PLUS ? 0 : op == LEX_TOKEN_MINUS ? 1 : op == LEX_TOKEN_STAR ? 2 : 3;

	if (x64_isq(c, t))
		return x64_qbin(e, k, a, b);
	if (KIND(c, t) == AST_TYPE_FLOAT16 || KIND(c, t) == AST_TYPE_BFLOAT16) {
		a = x64_cvt(e, a, t, codegen_type_primitive(AST_TYPE_FLOAT));
		b = x64_cvt(e, b, t, codegen_type_primitive(AST_TYPE_FLOAT));
		return x64_cvt(e, x64_fbin(e, op, codegen_type_primitive(AST_TYPE_FLOAT), a, b), codegen_type_primitive(AST_TYPE_FLOAT), t);
	}

	if (x64_isld(c, t)) {
		static const int xf[4] = { X64_X87_ADD, X64_X87_SUB, X64_X87_MUL, X64_X87_DIV };
		int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8), rd = x64_ldnew(e);
		codegen_lvalue ma = x64_at(ra), mb = x64_at(rb), md = x64_at(rd);

		x64_x87(e, X64_X87_LDT, &ma, 10);
		x64_x87(e, X64_X87_LDT, &mb, 10);
		x64_x87(e, xf[k], NULL, 0);
		x64_x87(e, X64_X87_STPT, &md, 10);
		x64_tfree(e, ra);
		x64_tfree(e, rb);
		return x64_vr(rd);
	}
	x64_record_emit_three_registers(e, X64_OP_FADD + k, x64_fsz(c, t), a.register_number, a.register_number, b.register_number);
	x64_tfree(e, b.register_number);
	return a;
}

VPRIV codegen_value x64_fsqrt(codegen_context *e, ast_qualified_type t, codegen_value a)
{
	x64_record_emit_three_registers(e, X64_OP_FSQRT, x64_fsz(e->compile_context, t), a.register_number, a.register_number, X64_REGISTER_NONE);
	return a;
}

VPRIV codegen_value x64_root4(codegen_context *e, codegen_value a)
{
	/* walk_value_emit materializes double values in an SSE temporary. */
	x64_record_emit_three_registers(e, X64_OP_FSQRT, 8, a.register_number, a.register_number, X64_REGISTER_NONE);
	x64_record_emit_three_registers(e, X64_OP_FSQRT, 8, a.register_number, a.register_number, X64_REGISTER_NONE);
	return a;
}

VPRIV codegen_value x64_fneg(codegen_context *e, ast_qualified_type t, codegen_value a)
{
	cc *c = e->compile_context;

	if (x64_isq(c, t)) {
		/* the sign bit flipped in a copy */
		int ra = x64_toreg(e, &a, 8), rd = x64_ldnew(e);

		x64_copy(e, rd, ra, 16);
		x64_tfree(e, ra);
		{
			codegen_lvalue hi = x64_at(rd);

			hi.offset = 15;
			x64_record_emit_memory(e, X64_OP_LD, 1, R11, &hi, false);
			x64_record_emit_three_registers(e, X64_OP_XORI, 4, R11, R11, X64_REGISTER_NONE)->immediate = 0x80;
			x64_record_emit_memory(e, X64_OP_ST, 1, R11, &hi, false);
		}
		return x64_vr(rd);
	}

	if (x64_isld(c, t)) {
		int ra = x64_toreg(e, &a, 8), rd = x64_ldnew(e);
		codegen_lvalue ma = x64_at(ra), md = x64_at(rd);

		x64_x87(e, X64_X87_LDT, &ma, 10);
		x64_x87(e, X64_X87_CHS, NULL, 0);
		x64_x87(e, X64_X87_STPT, &md, 10);
		x64_tfree(e, ra);
		return x64_vr(rd);
	}
	x64_record_emit_move_immediate(e, R11, (KIND(c, t) == AST_TYPE_FLOAT16 || KIND(c, t) == AST_TYPE_BFLOAT16) ? 0x8000 : x64_fsz(c, t) == 4 ? (int64_t)0x80000000 : INT64_MIN);
	x64_record_emit_three_registers(e, X64_OP_MOV, x64_fsz(c, t), XMM15, R11, X64_REGISTER_NONE);
	x64_record_emit_three_registers(e, X64_OP_FXOR, 8, a.register_number, a.register_number, XMM15);
	return a;
}

/* a op b as 0 or 1: false when unordered, but for != */
VPRIV codegen_value x64_fcmp(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	cc *c = e->compile_context;
	bool swap = op == LEX_TOKEN_LESS || op == LEX_TOKEN_LESS_EQUAL;	/* a < b is b > a: "above" is false for NaN */
	int cc = op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ? X64_CONDITION_A : X64_CONDITION_AE;
	int rd;

	if (x64_isq(c, t))
		return x64_qcmp(e, op, a, b);
	if (KIND(c, t) == AST_TYPE_FLOAT16 || KIND(c, t) == AST_TYPE_BFLOAT16) {
		a = x64_cvt(e, a, t, codegen_type_primitive(AST_TYPE_FLOAT));
		b = x64_cvt(e, b, t, codegen_type_primitive(AST_TYPE_FLOAT));
		return x64_fcmp(e, op, codegen_type_primitive(AST_TYPE_FLOAT), a, b);
	}
	if (op == WALK_FLOAT_COMPARE_UNORDERED)
		cc = X64_CONDITION_P;
	else if (op == WALK_FLOAT_COMPARE_ORDERED_NOT_EQUAL)
		cc = X64_CONDITION_NE;
	if (x64_isld(c, t)) {
		int ra = x64_toreg(e, &a, 8), rb = x64_toreg(e, &b, 8);
		codegen_lvalue ma = x64_at(ra), mb = x64_at(rb);

		/* fucomip compares st0 with st1: st0 is the left of the comparison */
		x64_x87(e, X64_X87_LDT, swap ? &ma : &mb, 10);
		x64_x87(e, X64_X87_LDT, swap ? &mb : &ma, 10);
		x64_x87(e, X64_X87_UCOMIP, NULL, 0);
		x64_x87(e, X64_X87_POP, NULL, 0);
		x64_tfree(e, ra);
		x64_tfree(e, rb);
	} else {
		if (swap)
			x64_record_emit_three_registers(e, X64_OP_FCMP, x64_fsz(c, t), X64_REGISTER_NONE, b.register_number, a.register_number);
		else
			x64_record_emit_three_registers(e, X64_OP_FCMP, x64_fsz(c, t), X64_REGISTER_NONE, a.register_number, b.register_number);
		x64_tfree(e, a.register_number);
		x64_tfree(e, b.register_number);
	}
	rd = x64_treg(e);
	if (op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL)
		x64_record_emit_three_registers(e, X64_OP_FSETCC, 4, rd, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = op == LEX_TOKEN_EXCLAMATION_EQUAL;
	else
		x64_record_emit_three_registers(e, X64_OP_SETCC, 4, rd, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = (uint8_t)cc;
	return x64_vr(rd);
}


/* ---------------- __float128: libgcc calls ---------------- */

/*
 * An argument or result of a soft-float call: Q a __float128 object (its
 * address in r; one xmm register), X an x87 long double object (address;
 * 16 bytes on the stack in, st(0) out), D/F a double/float in xmm r, I an
 * integer in r (64 bits), W an __int128 object (address; rdi:rsi in,
 * rax:rdx out).
 */
enum { TQ, TX, TD, TF, TI, TW };
typedef struct { int k, r; } tfarg;

static codegen_value x64_tfcall(codegen_context *e, int fn, const tfarg *a, int n, int rk)
{
	x64_nofp(e);
	x64_lowering_context *X = e->x64_context;
	uint32_t saved, fsaved;
	int nx = 0, ng = 0, pad, stk = 0, res;
	codegen_lvalue out = { 0 };
	record *i;

	for (int k = 0; k < n; k++)
		x64_tfree(e, a[k].r);	/* (the values stay in their registers until loaded below) */
	saved = X->live & X64_TEMPORARIES, fsaved = X->live & X64_FLOAT_TEMPORARIES;
	if (rk == TQ || rk == TX || rk == TW)
		out = x64_tmpslot(e, 16);
	e->has_calls = true;
	for (int q = 0; q < 16; q++)
		if (saved & (1u << q)) {
			x64_record_emit_three_registers(e, X64_OP_PUSH, 8, X64_REGISTER_NONE, q, X64_REGISTER_NONE);
			X->pushes++;
		}
	for (int q = XMM8; q < XMM15; q++)
		if (fsaved & (1u << q)) {
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

			x64_record_emit(e, X64_OP_ADJSP)->immediate = -8;
			x64_record_emit_memory(e, X64_OP_ST, 8, q, &d, false);
			X->pushes++;
		}
	for (int k = 0; k < n; k++)
		stk += a[k].k == TX ? 16 : 0;
	pad = X->pushes & 1 ? 8 : 0;
	if (pad + stk)
		x64_record_emit(e, X64_OP_ADJSP)->immediate = -(pad + stk);
	for (int k = 0, so = 0; k < n; k++) {
		codegen_lvalue m = x64_at(a[k].r);

		switch (a[k].k) {
		case TQ:
			x64_record_emit_memory(e, X64_OP_LD, 16, XMM0 + nx++, &m, false);
			break;
		case TX:
			for (int w = 0; w < 2; w++) {
				codegen_lvalue s = m, d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP, .offset = so + 8 * w };

				s.offset = 8 * w;
				x64_record_emit_memory(e, X64_OP_LD, 8, R11, &s, false);
				x64_record_emit_memory(e, X64_OP_ST, 8, R11, &d, false);
			}
			so += 16;
			break;
		case TD: case TF:
			x64_record_emit_three_registers(e, X64_OP_MOV, 8, XMM0 + nx++, a[k].r, X64_REGISTER_NONE);
			break;
		case TI:
			x64_mov(e, x64_argument_register(ng++), a[k].r);
			break;
		case TW: {
			codegen_lvalue hi = m;

			hi.offset = 8;
			x64_record_emit_memory(e, X64_OP_LD, 8, x64_argument_register(ng + 1), &hi, false);
			x64_record_emit_memory(e, X64_OP_LD, 8, x64_argument_register(ng), &m, false);
			ng += 2;
			break;
		}
		}
	}
	i = x64_record_emit(e, X64_OP_CALL);
	i->reference = CODEGEN_FIXED_REFERENCE(fn);
	switch (rk) {
	case TQ:
		x64_record_emit_memory(e, X64_OP_ST, 16, XMM0, &out, false);
		break;
	case TX:
		x64_x87st(e, &out);
		break;
	case TW: {
		codegen_lvalue hi = out;

		hi.offset += 8;
		x64_record_emit_memory(e, X64_OP_ST, 8, RAX, &out, false);
		x64_record_emit_memory(e, X64_OP_ST, 8, RDX, &hi, false);
		break;
	}
	case TD: case TF:
		x64_record_emit_three_registers(e, X64_OP_MOV, 8, XMM15, XMM0, X64_REGISTER_NONE);
		break;
	default:
		x64_mov(e, R11, RAX);
		break;
	}
	if (pad + stk)
		x64_record_emit(e, X64_OP_ADJSP)->immediate = pad + stk;
	for (int q = XMM15 - 1; q >= XMM8; q--)
		if (fsaved & (1u << q)) {
			codegen_lvalue s2 = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = RSP };

			x64_record_emit_memory(e, X64_OP_LD, 8, q, &s2, false);
			x64_record_emit(e, X64_OP_ADJSP)->immediate = 8;
			X->pushes--;
		}
	for (int q = 15; q >= 0; q--)
		if (saved & (1u << q)) {
			x64_record_emit_three_registers(e, X64_OP_POP, 8, q, X64_REGISTER_NONE, X64_REGISTER_NONE);
			X->pushes--;
		}
	if (rk == TQ || rk == TX || rk == TW)
		return x64_vr(x64_laddr(e, &out));
	if (rk == TD || rk == TF) {
		res = x64_ftreg(e);
		x64_record_emit_three_registers(e, X64_OP_MOV, 8, res, XMM15, X64_REGISTER_NONE);
		return x64_vr(res);
	}
	res = x64_treg(e);
	x64_mov(e, res, R11);
	return x64_vr(res);
}

/* a op b on __float128 objects */
static codegen_value x64_qbin(codegen_context *e, int k, codegen_value a, codegen_value b)
{
	static const int fn[4] = { CODEGEN_LIBRARY_ADDTF3, CODEGEN_LIBRARY_SUBTF3, CODEGEN_LIBRARY_MULTF3, CODEGEN_LIBRARY_DIVTF3 };
	tfarg ar[2] = { { TQ, x64_toreg(e, &a, 8) }, { TQ, x64_toreg(e, &b, 8) } };

	return x64_tfcall(e, fn[k], ar, 2, TQ);
}

/* a op b on __float128 objects as 0 or 1 (false when unordered, but for !=) */
static codegen_value x64_qcmp(codegen_context *e, int op, codegen_value a, codegen_value b)
{
	if (op == WALK_FLOAT_COMPARE_ORDERED_NOT_EQUAL) {
		/* Both ordered inequalities are false for NaN. Keep the original
		   addresses live across the first helper call for the second one. */
		int ar = x64_toreg(e, &a, 8), br = x64_toreg(e, &b, 8);
		int ac = x64_treg(e), bc = x64_treg(e);
		x64_mov(e, ac, ar);
		x64_mov(e, bc, br);
		codegen_value lt = x64_qcmp(e, LEX_TOKEN_LESS, x64_vr(ac), x64_vr(bc));
		codegen_value gt = x64_qcmp(e, LEX_TOKEN_GREATER, a, b);
		x64_record_emit_three_registers(e, X64_OP_OR, 4, lt.register_number, lt.register_number, gt.register_number);
		x64_tfree(e, gt.register_number);
		return lt;
	}
	int fn = op == WALK_FLOAT_COMPARE_UNORDERED ? CODEGEN_LIBRARY_UNORDTF2 : op == LEX_TOKEN_EQUAL_EQUAL ? CODEGEN_LIBRARY_EQTF2 : op == LEX_TOKEN_EXCLAMATION_EQUAL ? CODEGEN_LIBRARY_NETF2 : op == LEX_TOKEN_LESS ? CODEGEN_LIBRARY_LTTF2 :
		 op == LEX_TOKEN_LESS_EQUAL ? CODEGEN_LIBRARY_LETF2 : op == LEX_TOKEN_GREATER ? CODEGEN_LIBRARY_GTTF2 : CODEGEN_LIBRARY_GETF2;
	int cc = op == WALK_FLOAT_COMPARE_UNORDERED ? X64_CONDITION_NE : op == LEX_TOKEN_EQUAL_EQUAL ? X64_CONDITION_E : op == LEX_TOKEN_EXCLAMATION_EQUAL ? X64_CONDITION_NE : op == LEX_TOKEN_LESS ? X64_CONDITION_L :
		 op == LEX_TOKEN_LESS_EQUAL ? X64_CONDITION_LE : op == LEX_TOKEN_GREATER ? X64_CONDITION_G : X64_CONDITION_GE;
	tfarg ar[2] = { { TQ, x64_toreg(e, &a, 8) }, { TQ, x64_toreg(e, &b, 8) } };
	codegen_value r = x64_tfcall(e, fn, ar, 2, TI);

	x64_record_emit_three_registers(e, X64_OP_TEST, 4, X64_REGISTER_NONE, r.register_number, X64_REGISTER_NONE);
	x64_record_emit_three_registers(e, X64_OP_SETCC, 4, r.register_number, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = (uint8_t)cc;
	return r;
}

/* a new __float128 zero */
static codegen_value x64_qzero(codegen_context *e)
{
	codegen_lvalue l = x64_tmpslot(e, 16);
	int r = x64_laddr(e, &l);

	x64_zero(e, r, 16);
	return x64_vr(r);
}

/* conversions with a __float128 side (to: not _Bool) */
static codegen_value x64_qcvt(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;
	bool fq = x64_isq(c, from);
	ast_qualified_type o = fq ? to : from;	/* the other side */
	int os = walk_type_size(c, o), k = KIND(c, o);
	bool sg = walk_type_is_signed(c, o);

	if (fq && x64_isq(c, to))
		return v;
	if (codegen_type_is_float(c, o)) {
		tfarg a = { k == AST_TYPE_LONG_DOUBLE ? TX : fq ? TQ : k == AST_TYPE_FLOAT ? TF : TD,
			    v.kind == CODEGEN_VALUE_REGISTER ? v.register_number : x64_toreg(e, &v, 8) };

		if (fq)
			return x64_tfcall(e, k == AST_TYPE_LONG_DOUBLE ? CODEGEN_LIBRARY_TRUNCTFXF2 : k == AST_TYPE_FLOAT ? CODEGEN_LIBRARY_TRUNCTFSF2 : CODEGEN_LIBRARY_TRUNCTFDF2,
					  &(tfarg){ TQ, a.r }, 1, k == AST_TYPE_LONG_DOUBLE ? TX : k == AST_TYPE_FLOAT ? TF : TD);
		return x64_tfcall(e, k == AST_TYPE_LONG_DOUBLE ? CODEGEN_LIBRARY_EXTENDXFTF2 : k == AST_TYPE_FLOAT ? CODEGEN_LIBRARY_EXTENDSFTF2 : CODEGEN_LIBRARY_EXTENDDFTF2,
				  &a, 1, TQ);
	}
	if (k == AST_TYPE_INT128 || k == AST_TYPE_UINT128) {
		tfarg a = { fq ? TQ : TW, x64_toreg(e, &v, 8) };

		return x64_tfcall(e, fq ? (sg ? CODEGEN_LIBRARY_FIXTFTI : CODEGEN_LIBRARY_FIXUNSTFTI) : (sg ? CODEGEN_LIBRARY_FLOATTITF : CODEGEN_LIBRARY_FLOATUNTITF), &a, 1, fq ? TW : TQ);
	}
	if (fq) {
		/* to an integer: through 32 or 64 bits, unsigned 32 by the 64-bit fix */
		tfarg a = { TQ, x64_toreg(e, &v, 8) };
		int fn = os == 8 ? (sg ? CODEGEN_LIBRARY_FIXTFDI : CODEGEN_LIBRARY_FIXUNSTFDI) : os == 4 && !sg ? CODEGEN_LIBRARY_FIXTFDI : CODEGEN_LIBRARY_FIXTFSI;
		codegen_value r = x64_tfcall(e, fn, &a, 1, TI);

		if (os < 8)
			x64_ext(e, r.register_number, os, sg);
		return r;
	}
	{
		int r = x64_toreg(e, &v, 8);
		tfarg a = { TI, r };

		if (os < 8)
			x64_ext(e, r, os, sg);
		return x64_tfcall(e, os < 8 ? CODEGEN_LIBRARY_FLOATDITF : sg ? CODEGEN_LIBRARY_FLOATDITF : CODEGEN_LIBRARY_FLOATUNDITF, &a, 1, TQ);
	}
}

/* ---------------- conversions ---------------- */

/* an integer in a register (size bytes, sgn) to the float of fs bytes */
static codegen_value x64_i2f(codegen_context *e, int r, int size, bool sgn, int fs)
{
	int x = x64_ftreg(e);

	if (sgn || size < 8) {
		if (!sgn && size == 4)
			x64_ext(e, r, 4, false);	/* (zero-extended: converted as 64 bits) */
		x64_record_emit_three_registers(e, X64_OP_CVTI2F, fs, x, r, X64_REGISTER_NONE)->shift = (uint8_t)(sgn && size <= 4 ? 4 : 8);
	} else {
		/* unsigned 64 bits: halved (keeping the low bit) and doubled when the top bit is set */
		uint32_t lbig = walk_label_new(e), ldone = walk_label_new(e);

		x64_record_emit_three_registers(e, X64_OP_TEST, 8, X64_REGISTER_NONE, r, X64_REGISTER_NONE);
		x64_jcc(e, X64_CONDITION_S, lbig);
		x64_record_emit_three_registers(e, X64_OP_CVTI2F, fs, x, r, X64_REGISTER_NONE)->shift = 8;
		x64_jump(e, ldone);
		x64_label(e, lbig);
		x64_mov(e, R11, r);
		x64_record_emit_three_registers(e, X64_OP_SHRI, 8, R11, R11, X64_REGISTER_NONE)->immediate = 1;
		x64_record_emit_three_registers(e, X64_OP_ANDI, 8, r, r, X64_REGISTER_NONE)->immediate = 1;
		x64_record_emit_three_registers(e, X64_OP_OR, 8, R11, R11, r);
		x64_record_emit_three_registers(e, X64_OP_CVTI2F, fs, x, R11, X64_REGISTER_NONE)->shift = 8;
		x64_record_emit_three_registers(e, X64_OP_FADD, fs, x, x, x);
		x64_label(e, ldone);
	}
	x64_tfree(e, r);
	return x64_vr(x);
}

/* a float of fs bytes in x to an integer of ts bytes */
static codegen_value x64_f2i(codegen_context *e, int x, int fs, int ts, bool sgn)
{
	int r = x64_treg(e);

	if (sgn || ts < 8) {
		x64_record_emit_three_registers(e, X64_OP_CVTF2I, ts > 4 || !sgn ? 8 : 4, r, x, X64_REGISTER_NONE)->shift = (uint8_t)fs;
	} else {
		/* unsigned 64 bits: values from 2^63 up are converted less 2^63, the top bit set after */
		uint32_t lbig = walk_label_new(e), ldone = walk_label_new(e);

		x64_record_emit_move_immediate(e, R11, fs == 4 ? 0x5f000000 : 0x43e0000000000000);
		x64_record_emit_three_registers(e, X64_OP_MOV, fs, XMM15, R11, X64_REGISTER_NONE);
		x64_record_emit_three_registers(e, X64_OP_FCMP, fs, X64_REGISTER_NONE, x, XMM15);
		x64_jcc(e, X64_CONDITION_AE, lbig);
		x64_record_emit_three_registers(e, X64_OP_CVTF2I, 8, r, x, X64_REGISTER_NONE)->shift = (uint8_t)fs;
		x64_jump(e, ldone);
		x64_label(e, lbig);
		x64_record_emit_three_registers(e, X64_OP_FSUB, fs, x, x, XMM15);
		x64_record_emit_three_registers(e, X64_OP_CVTF2I, 8, r, x, X64_REGISTER_NONE)->shift = (uint8_t)fs;
		x64_record_emit_move_immediate(e, R11, INT64_MIN);
		x64_record_emit_three_registers(e, X64_OP_XOR, 8, r, r, R11);
		x64_label(e, ldone);
	}
	x64_tfree(e, x);
	if (ts < 4)
		x64_ext(e, r, ts, sgn);
	return x64_vr(r);
}

/* v of type from as type to, a float on at least one side (to may be _Bool) */
VPRIV codegen_value x64_cvt(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;
	bool ff = codegen_type_is_float(c, from), tf = codegen_type_is_float(c, to), fl = x64_isld(c, from), tl = x64_isld(c, to);
	int fs = walk_type_size(c, from), ts = walk_type_size(c, to);

	if (KIND(c, from) == AST_TYPE_BFLOAT16) {
		if (KIND(c, to) == AST_TYPE_BFLOAT16) return v;
		v = x64_half_call(e, CODEGEN_LIBRARY_EXTENDBFSF2, v, from, codegen_type_primitive(AST_TYPE_FLOAT));
		return x64_cvt(e, v, codegen_type_primitive(AST_TYPE_FLOAT), to);
	}
	if (KIND(c, to) == AST_TYPE_BFLOAT16) {
		if (x64_isq(c, from))
			return x64_tfcall(e, CODEGEN_LIBRARY_TRUNCTFBF2,
					  &(tfarg){ TQ, x64_toreg(e, &v, 8) }, 1, TF);
		if (KIND(c, from) == AST_TYPE_FLOAT16)
			v = x64_cvt(e, v, from, codegen_type_primitive(AST_TYPE_FLOAT)), from = codegen_type_primitive(AST_TYPE_FLOAT);
		if (!ff)
			v = x64_cvt(e, v, from, codegen_type_primitive(AST_TYPE_DOUBLE)), from = codegen_type_primitive(AST_TYPE_DOUBLE);
		return x64_half_call(e, x64_isld(c, from) ? CODEGEN_LIBRARY_TRUNCXFBF2 : KIND(c, from) == AST_TYPE_FLOAT ? CODEGEN_LIBRARY_TRUNCSFBF2 : CODEGEN_LIBRARY_TRUNCDFBF2, v, from, to);
	}
	if (KIND(c, from) == AST_TYPE_FLOAT16) {
		if (KIND(c, to) == AST_TYPE_FLOAT16)
			return v;
		v = x64_half_call(e, CODEGEN_LIBRARY_EXTENDHFSF2, v, from, codegen_type_primitive(AST_TYPE_FLOAT));
		return x64_cvt(e, v, codegen_type_primitive(AST_TYPE_FLOAT), to);
	}
	if (KIND(c, to) == AST_TYPE_FLOAT16) {
		if (x64_isq(c, from))
			return x64_tfcall(e, CODEGEN_LIBRARY_TRUNCTFHF2,
					  &(tfarg){ TQ, x64_toreg(e, &v, 8) }, 1, TF);
		if (!ff)
			v = x64_cvt(e, v, from, codegen_type_primitive(AST_TYPE_DOUBLE)), from = codegen_type_primitive(AST_TYPE_DOUBLE);
		return x64_half_call(e, x64_isld(c, from) ? CODEGEN_LIBRARY_TRUNCXFHF2 : KIND(c, from) == AST_TYPE_FLOAT ? CODEGEN_LIBRARY_TRUNCSFHF2 : CODEGEN_LIBRARY_TRUNCDFHF2, v, from, to);
	}
	if (x64_isq(c, from) && KIND(c, to) == AST_TYPE_BOOL)
		return x64_qcmp(e, LEX_TOKEN_EXCLAMATION_EQUAL, v, x64_qzero(e));
	if (x64_isq(c, from) || x64_isq(c, to))
		return x64_qcvt(e, v, from, to);
	if (KIND(c, to) == AST_TYPE_BOOL) {
		int rd;

		if (fl) {
			int ra = x64_toreg(e, &v, 8);
			codegen_lvalue m = x64_at(ra);

			x64_x87(e, X64_X87_LDZ, NULL, 0);
			x64_x87(e, X64_X87_LDT, &m, 10);
			x64_x87(e, X64_X87_UCOMIP, NULL, 0);
			x64_x87(e, X64_X87_POP, NULL, 0);
			x64_tfree(e, ra);
		} else {
			x64_record_emit_three_registers(e, X64_OP_FXOR, 8, XMM15, XMM15, XMM15);
			x64_record_emit_three_registers(e, X64_OP_FCMP, x64_fsz(c, from), X64_REGISTER_NONE, v.register_number, XMM15);
			x64_tfree(e, v.register_number);
		}
		rd = x64_treg(e);
		x64_record_emit_three_registers(e, X64_OP_FSETCC, 4, rd, X64_REGISTER_NONE, X64_REGISTER_NONE)->condition = 1;
		return x64_vr(rd);
	}
	if (ff && tf) {
		if (fl == tl) {
			if (!fl && fs != ts)
				x64_record_emit_three_registers(e, X64_OP_CVTF2F, ts, v.register_number, v.register_number, X64_REGISTER_NONE);
			return v;
		}
		if (tl) {
			/* float, double -> long double: through memory */
			codegen_lvalue t = x64_tmpslot(e, 8);
			int rd = x64_ldnew(e);
			codegen_lvalue md = x64_at(rd);

			x64_record_emit_memory(e, X64_OP_ST, fs, v.register_number, &t, false);
			x64_tfree(e, v.register_number);
			x64_x87(e, X64_X87_LD, &t, fs);
			x64_x87(e, X64_X87_STPT, &md, 10);
			return x64_vr(rd);
		}
		{
			int ra = x64_toreg(e, &v, 8), x;
			codegen_lvalue m = x64_at(ra), t = x64_tmpslot(e, 8);

			x64_x87(e, X64_X87_LDT, &m, 10);
			x64_x87(e, X64_X87_STP, &t, ts);
			x64_tfree(e, ra);
			x = x64_ftreg(e);
			x64_record_emit_memory(e, X64_OP_LD, ts, x, &t, false);
			return x64_vr(x);
		}
	}
	if (tf) {
		bool sg = walk_type_is_signed(c, from);

		if (v.kind == CODEGEN_VALUE_CONSTANT && !tl) {
			/* folded as C converts (round to nearest) */
			double d = sg ? (double)v.integer : (double)(u128)(uint64_t)v.integer;
			float f = sg ? (float)v.integer : (float)(uint64_t)v.integer;
			uint64_t bits = 0;

			if (ts == 4)
				memcpy(&bits, &f, 4);
			else
				memcpy(&bits, &d, 8);
			return x64_fconst(e, bits, ts);
		}
		if (tl) {
			int r = x64_toreg(e, &v, 8), rd;
			codegen_lvalue t = x64_tmpslot(e, 8), md;
			uint32_t ldone = walk_label_new(e);

			if (fs < 8)
				x64_ext(e, r, fs, sg);
			x64_record_emit_memory(e, X64_OP_ST, 8, r, &t, false);
			x64_x87(e, X64_X87_ILD, &t, 8);
			if (!sg && fs == 8) {
				/* fild took the top bit as a sign: add 2^64 back */
				codegen_lvalue k = x64_tmpslot(e, 8);

				x64_record_emit_three_registers(e, X64_OP_TEST, 8, X64_REGISTER_NONE, r, X64_REGISTER_NONE);
				x64_jcc(e, X64_CONDITION_NS, ldone);
				x64_record_emit_move_immediate(e, R11, 0x5f800000);
				x64_record_emit_memory(e, X64_OP_ST, 4, R11, &k, false);
				x64_x87(e, X64_X87_LD, &k, 4);
				x64_x87(e, X64_X87_ADD, NULL, 0);
				x64_label(e, ldone);
			}
			x64_tfree(e, r);
			rd = x64_ldnew(e);
			md = x64_at(rd);
			x64_x87(e, X64_X87_STPT, &md, 10);
			return x64_vr(rd);
		}
		{
			int r = x64_toreg(e, &v, 8);

			if (fs < 4)
				x64_ext(e, r, fs, sg);
			return x64_i2f(e, r, fs < 4 ? 4 : fs, sg, ts);
		}
	}
	/* float -> integer */
	if (fl) {
		/* truncation: the control word's rounding set to zero around fistp */
		int ra = x64_toreg(e, &v, 8), r;
		codegen_lvalue m = x64_at(ra), cw = x64_tmpslot(e, 8), cw2 = x64_tmpslot(e, 8), t = x64_tmpslot(e, 8);

		x64_x87(e, X64_X87_STCW, &cw, 2);
		x64_record_emit_memory(e, X64_OP_LD, 2, R11, &cw, false);
		x64_record_emit_three_registers(e, X64_OP_ORI, 4, R11, R11, X64_REGISTER_NONE)->immediate = 0xc00;
		x64_record_emit_memory(e, X64_OP_ST, 2, R11, &cw2, false);
		x64_x87(e, X64_X87_LDCW, &cw2, 2);
		x64_x87(e, X64_X87_LDT, &m, 10);
		x64_tfree(e, ra);
		r = x64_treg(e);
		if (ts == 8 && !walk_type_is_signed(c, to)) {
			/* fistp only stores signed integers. Split the unsigned range at
			 * 2^63, subtracting that exact value before the signed conversion. */
			codegen_lvalue bias = x64_tmpslot(e, 8);
			uint32_t low = walk_label_new(e), done = walk_label_new(e);

			x64_record_emit_move_immediate(e, R11, 0x5f000000); /* float 2^63 */
			x64_record_emit_memory(e, X64_OP_ST, 4, R11, &bias, false);
			x64_x87(e, X64_X87_LD, &bias, 4);
			x64_x87(e, X64_X87_UCOMIP, NULL, 0); /* bias > input; leaves input */
			x64_jcc(e, X64_CONDITION_A, low);
			x64_x87(e, X64_X87_LD, &bias, 4);
			x64_x87(e, X64_X87_SUB, NULL, 0);
			x64_x87(e, X64_X87_ISTP, &t, 8);
			x64_record_emit_memory(e, X64_OP_LD, 8, r, &t, false);
			x64_record_emit_move_immediate(e, R11, UINT64_C(1) << 63);
			x64_record_emit_three_registers(e, X64_OP_XOR, 8, r, r, R11);
			x64_jump(e, done);
			x64_label(e, low);
			x64_x87(e, X64_X87_ISTP, &t, 8);
			x64_record_emit_memory(e, X64_OP_LD, 8, r, &t, false);
			x64_label(e, done);
		} else {
			x64_x87(e, X64_X87_ISTP, &t, 8);
			x64_record_emit_memory(e, X64_OP_LD, 8, r, &t, false);
		}
		x64_x87(e, X64_X87_LDCW, &cw, 2);
		if (ts < 8)
			x64_ext(e, r, ts, walk_type_is_signed(c, to));
		return x64_vr(r);
	}
	return x64_f2i(e, v.register_number, fs, ts, walk_type_is_signed(c, to));
}
