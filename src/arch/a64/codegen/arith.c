/* vcc code generator: integer helpers, truth tests, casts, arithmetic */
#include "../../../codegen/codegen-internal.h"

/* ---------------- integer helpers ---------------- */

/* extend the low `from` bytes of r to 64 bits */
VPRIV void extend(codegen_context *e, int rd, int rs, int from, bool sgn)
{
	switch (from) {
	case 1: opbf(e, sgn ? I_SBFM : I_UBFM, 1, rd, rs, 0, 7); break;
	case 2: opbf(e, sgn ? I_SBFM : I_UBFM, 1, rd, rs, 0, 15); break;
	case 4:
		if (sgn)
			opbf(e, I_SBFM, 1, rd, rs, 0, 31);
		else
			op3(e, I_MOV, 0, rd, rs, 0);
		break;
	default:
		mov(e, 1, rd, rs);
	}
}

VPRIV codegen_value fold_int_bin(codegen_context *e, int op, ast_qualified_type ot, ast_qualified_type rt, i128 l, i128 r, bool *ok)
{
	cc *c = e->compile_context;
	bool sg = codegen_type_is_signed(c, ot);
	i128 v = 0;
	u128 ul = (u128)codegen_integer_truncate(c, l, ot), ur = (u128)codegen_integer_truncate(c, r, ot);

	if (!sg && codegen_type_size(c, ot) < 16) {
		ul = (u128)codegen_integer_truncate(c, l, ot) & (((u128)1 << (codegen_type_size(c, ot) * 8)) - 1);
		ur = (u128)codegen_integer_truncate(c, r, ot) & (((u128)1 << (codegen_type_size(c, ot) * 8)) - 1);
	}
	*ok = true;
	switch (op) {
	case LEX_TOKEN_PLUS: v = (i128)((u128)l + (u128)r); break;
	case LEX_TOKEN_MINUS: v = (i128)((u128)l - (u128)r); break;
	case LEX_TOKEN_STAR: v = (i128)((u128)l * (u128)r); break;
	case LEX_TOKEN_SLASH:
	case LEX_TOKEN_PERCENT:
		if (r == 0 || (sg && r == -1)) {
			*ok = false;
			return codegen_value_none();
		}
		if (sg)
			v = op == LEX_TOKEN_SLASH ? l / r : l % r;
		else
			v = (i128)(op == LEX_TOKEN_SLASH ? ul / ur : ul % ur);
		break;
	case LEX_TOKEN_LESS_LESS:
		if (r < 0 || (uint64_t)r >= codegen_type_size(c, rt) * 8) {
			*ok = false;
			return codegen_value_none();
		}
		v = (i128)((u128)l << r);
		break;
	case LEX_TOKEN_GREATER_GREATER:
		if (r < 0 || (uint64_t)r >= codegen_type_size(c, rt) * 8) {
			*ok = false;
			return codegen_value_none();
		}
		v = codegen_type_is_signed(c, rt) ? codegen_integer_truncate(c, l, rt) >> r : (i128)(ul >> r);
		break;
	case LEX_TOKEN_AMPERSAND: v = l & r; break;
	case LEX_TOKEN_PIPE: v = l | r; break;
	case LEX_TOKEN_CARET: v = l ^ r; break;
	case LEX_TOKEN_LESS: v = sg ? codegen_integer_truncate(c, l, ot) < codegen_integer_truncate(c, r, ot) : ul < ur; break;
	case LEX_TOKEN_LESS_EQUAL: v = sg ? codegen_integer_truncate(c, l, ot) <= codegen_integer_truncate(c, r, ot) : ul <= ur; break;
	case LEX_TOKEN_GREATER: v = sg ? codegen_integer_truncate(c, l, ot) > codegen_integer_truncate(c, r, ot) : ul > ur; break;
	case LEX_TOKEN_GREATER_EQUAL: v = sg ? codegen_integer_truncate(c, l, ot) >= codegen_integer_truncate(c, r, ot) : ul >= ur; break;
	case LEX_TOKEN_EQUAL_EQUAL: v = ul == ur; break;
	case LEX_TOKEN_EXCLAMATION_EQUAL: v = ul != ur; break;
	default:
		*ok = false;
		return codegen_value_none();
	}
	return codegen_value_constant(codegen_integer_truncate(c, v, rt));
}

static int cmp_cond(int op, bool sg)
{
	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: return C_EQ;
	case LEX_TOKEN_EXCLAMATION_EQUAL: return C_NE;
	case LEX_TOKEN_LESS: return sg ? C_LT : C_LO;
	case LEX_TOKEN_LESS_EQUAL: return sg ? C_LE : C_LS;
	case LEX_TOKEN_GREATER: return sg ? C_GT : C_HI;
	default: return sg ? C_GE : C_HS;
	}
}

/* compare a with b (width x); returns the condition for "op is true" */
VPRIV int emit_cmp(codegen_context *e, int op, int x, bool sg, codegen_value *a, codegen_value *b)
{
	int ra = toreg(e, a);

	if (b->kind == CODEGEN_VALUE_CONSTANT && b->integer >= -4095 && b->integer <= 4095) {
		if (b->integer < 0)
			opi(e, I_ADDSI, x, ZR, ra, (int64_t)-b->integer);
		else
			opi(e, I_SUBSI, x, ZR, ra, (int64_t)b->integer);
	} else {
		int rb = toreg(e, b);

		op3(e, I_SUBS, x, ZR, ra, rb);
	}
	return cmp_cond(op, sg);
}

/* 128-bit compare: returns the condition */
VPRIV int emit_cmp2(codegen_context *e, int op, bool sg, codegen_value *a, codegen_value *b)
{
	toreg2(e, a, sg);
	toreg2(e, b, sg);
	if (op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL) {
		op3(e, I_EOR, 1, a->register_number, a->register_number, b->register_number);
		op3(e, I_EOR, 1, a->register_high, a->register_high, b->register_high);
		op3(e, I_ORR, 1, a->register_number, a->register_number, a->register_high);
		op3(e, I_SUBS, 1, ZR, a->register_number, ZR);
		return op == LEX_TOKEN_EQUAL_EQUAL ? C_EQ : C_NE;
	}
	if (op == LEX_TOKEN_GREATER || op == LEX_TOKEN_LESS_EQUAL) {
		codegen_value t = *a;

		*a = *b;
		*b = t;
		op = op == LEX_TOKEN_GREATER ? LEX_TOKEN_LESS : LEX_TOKEN_GREATER_EQUAL;
	}
	op3(e, I_SUBS, 1, ZR, a->register_number, b->register_number);
	op3(e, I_SBCS, 1, ZR, a->register_high, b->register_high);
	return cmp_cond(op, sg);
}

/* ---------------- truth tests ---------------- */

/* branch to l if (v != 0) == nz; consumes v.  Returns the constant truth or -1. */
VPRIV int test_zero(codegen_context *e, codegen_value *v, ast_qualified_type t, uint32_t l, bool nz)
{
	cc *c = e->compile_context;
	uint64_t sz = codegen_type_size(c, t);
	int r;

	if (codegen_type_is_bitint_big(c, t)) {
		*v = a64_bigbool(e, *v, t);
		t = codegen_type_primitive(AST_TYPE_INT); sz = 4;
	}
	if (KIND(c, t) == AST_TYPE_FLOAT16 || KIND(c, t) == AST_TYPE_BFLOAT16) {
		*v = f_tobool(e, *v, t);
		t = codegen_type_primitive(AST_TYPE_INT);
		sz = 4;
	}
	if (v->kind == CODEGEN_VALUE_CONSTANT || v->kind == CODEGEN_VALUE_ADDRESS) {
		bool truth = v->kind == CODEGEN_VALUE_ADDRESS || v->integer != 0;

		if (truth == nz)
			jump(e, l);
		return truth;
	}
	if (v->kind == CODEGEN_VALUE_NONE)
		return -1;
	if (codegen_type_is_long_double(c, t)) {
		*v = ld_compare(e, LEX_TOKEN_EXCLAMATION_EQUAL, t, *v, codegen_value_constant(0));
		t = codegen_type_primitive(AST_TYPE_INT);
		sz = 4;
	}
	if (codegen_type_is_float(c, t)) {
		int d = fpk(e, t), cond = f_cmp(e, LEX_TOKEN_EXCLAMATION_EQUAL, d, v, NULL);

		bcond(e, nz ? cond : C_EQ, l);
		val_free(e, v);
		return -1;
	}
	if (v->kind == CODEGEN_VALUE_REGISTER_PAIR) {
		op3(e, I_ORR, 1, R_X16, v->register_number, v->register_high);
		cbz(e, nz, 1, R_X16, l);
		val_free(e, v);
		return -1;
	}
	r = v->register_number;
	{
		int cond = last_cset(e, v);

		if (cond >= 0) {
			e->record_count--;
			bcond(e, nz ? cond : cond ^ 1, l);
			val_free(e, v);
			return -1;
		}
	}
	if (codegen_type_is_pointer(c, t) || sz == 8) {
		cbz(e, nz, 1, r, l);
	} else if (sz == 4) {
		cbz(e, nz, 0, r, l);
	} else {
		opi(e, I_ANDSI, 0, ZR, r, sz == 2 ? 0xffff : 0xff);
		bcond(e, nz ? C_NE : C_EQ, l);
	}
	val_free(e, v);
	return -1;
}

/* v as 0/1 (int) */
VPRIV codegen_value tobool(codegen_context *e, codegen_value v, ast_qualified_type t)
{
	cc *c = e->compile_context;
	uint64_t sz = codegen_type_size(c, t);
	int r;

	if (codegen_type_is_bitint_big(c, t)) return a64_bigbool(e, v, t);
	if (codegen_type_is_float(c, t))
		return f_tobool(e, v, t);
	if (v.kind == CODEGEN_VALUE_CONSTANT)
		return codegen_value_constant(v.integer != 0);
	if (v.kind == CODEGEN_VALUE_ADDRESS)
		return codegen_value_constant(1);
	if (v.kind == CODEGEN_VALUE_REGISTER_PAIR) {
		op3(e, I_ORR, 1, v.register_number, v.register_number, v.register_high);
		tfree(e, v.register_high);
		v = codegen_value_register(v.register_number);
		sz = 8;
	}
	r = toreg(e, &v);
	if (KIND(c, t) == AST_TYPE_BOOL || last_cset(e, &v) >= 0)
		return v;
	if (codegen_type_is_pointer(c, t) || sz == 8)
		op3(e, I_SUBS, 1, ZR, r, ZR);
	else if (sz == 4)
		op3(e, I_SUBS, 0, ZR, r, ZR);
	else
		opi(e, I_ANDSI, 0, ZR, r, sz == 2 ? 0xffff : 0xff);
	cset(e, r, C_NE);
	return v;
}

/* ---------------- casts ---------------- */

VPRIV codegen_value cast(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to, int kind)
{
	cc *c = e->compile_context;
	uint64_t fs = codegen_type_size(c, from), ts = codegen_type_size(c, to);

	if ((codegen_type_is_bitint_big(c, from) || codegen_type_is_bitint_big(c, to)) && kind != AST_CAST_VOID) {
		if (kind == AST_CAST_BOOL) return tobool(e, v, from);
		if (codegen_type_is_float(c, from) || codegen_type_is_float(c, to)) codegen_fatal_report(e, 0, "arbitrary-width ARM floating conversion is not supported yet");
		return a64_bigcvt(e, v, from, to);
	}
	if ((codegen_type_is_float(c, from) || codegen_type_is_float(c, to)) && kind != AST_CAST_VOID)
		return f_cast(e, v, from, to);
	switch (kind) {
	case AST_CAST_NOP:
	case AST_CAST_POINTER_TO_POINTER:
		if (v.kind == CODEGEN_VALUE_CONSTANT && !codegen_type_is_pointer(c, to))
			v.integer = codegen_integer_truncate(c, v.integer, to);
		return v;
	case AST_CAST_BOOL:
		return tobool(e, v, from);
	case AST_CAST_NULL_POINTER:
		val_free(e, &v);
		return codegen_value_constant(0);
	case AST_CAST_INTEGER:
	case AST_CAST_POINTER_TO_INTEGER:
	case AST_CAST_INTEGER_TO_POINTER:
		if (v.kind == CODEGEN_VALUE_CONSTANT) {
			if (kind == AST_CAST_INTEGER_TO_POINTER)
				return codegen_value_constant((i128)(uint64_t)codegen_integer_truncate(c, v.integer, from));
			return codegen_value_constant(codegen_integer_truncate(c, v.integer, kind == AST_CAST_POINTER_TO_INTEGER && ts == 16 ? codegen_type_primitive(AST_TYPE_UINT128) : to));
		}
		if (v.kind == CODEGEN_VALUE_ADDRESS) {
			if (ts == 8)
				return v;
			toreg(e, &v);
		}
		if (v.kind == CODEGEN_VALUE_NONE)
			return v;
		if (ts == 16 && fs < 16) {
			bool sg = kind == AST_CAST_INTEGER && codegen_type_is_signed(c, from);
			int r = toreg(e, &v);

			if (fs < 8)
				extend(e, r, r, (int)fs, sg);
			toreg2(e, &v, sg);
			return v;
		}
		if (fs == 16 && ts < 16) {
			if (v.kind == CODEGEN_VALUE_REGISTER_PAIR) {
				tfree(e, v.register_high);
				v = codegen_value_register(v.register_number);
			}
			return v;
		}
		if (ts > fs && last_cset(e, &v) < 0) {	/* a cset's 0/1 is already extended */
			int r = toreg(e, &v);

			extend(e, r, r, (int)fs, kind != AST_CAST_POINTER_TO_INTEGER && codegen_type_is_signed(c, from));
		}
		return v;
	case AST_CAST_VOID:
		val_free(e, &v);
		return codegen_value_none();
	default:
		return v;
	}
}

/* ---------------- arithmetic ---------------- */

VPRIV void shift_imm(codegen_context *e, int op, int x, int rd, int rn, int k)
{
	int w = x ? 64 : 32;

	k &= w - 1;
	if (op == LEX_TOKEN_LESS_LESS)
		opbf(e, I_UBFM, x, rd, rn, (w - k) % w, w - 1 - k);
	else if (op == I_ASRV)
		opbf(e, I_SBFM, x, rd, rn, k, w - 1);
	else
		opbf(e, I_UBFM, x, rd, rn, k, w - 1);
}

VPRIV codegen_value arith(codegen_context *e, int op, ast_qualified_type ty, codegen_value a, codegen_value b)
{
	cc *c = e->compile_context;
	int x = X(c, ty), ra, rb;
	bool sg = codegen_type_is_signed(c, ty);

	/* commutative: constant on the right */
	if (a.kind == CODEGEN_VALUE_CONSTANT && b.kind != CODEGEN_VALUE_CONSTANT && (op == LEX_TOKEN_PLUS || op == LEX_TOKEN_STAR || op == LEX_TOKEN_AMPERSAND || op == LEX_TOKEN_PIPE || op == LEX_TOKEN_CARET)) {
		codegen_value t = a;

		a = b;
		b = t;
	}
	if (b.kind == CODEGEN_VALUE_CONSTANT) {
		int id = em_bin_identity(c, ty, op, b.integer);

		if (id == EM_ID_LEFT) {
			if (a.kind == CODEGEN_VALUE_CONSTANT)
				return codegen_value_constant(codegen_integer_truncate(c, a.integer, ty));
			/* A 32-bit operation clears high bits even after a lazy narrowing cast. */
			if (!x) {
				ra = toreg(e, &a);
				extend(e, ra, ra, 4, false);
			}
			return a;
		}
		if (id == EM_ID_ZERO || id == EM_ID_ONES) {
			val_free(e, &a);
			return codegen_value_constant(id == EM_ID_ZERO ? 0 : codegen_integer_truncate(c, -1, ty));
		}
		if (e->unit->options->opt && !e->unit->cleanups_disabled && em_xor_not(c, ty, op, b.integer)) {
			ra = toreg(e, &a);
			op3(e, I_ORN, x, ra, ZR, ra);
			return a;
		}

		if (e->unit->options->opt && !e->unit->cleanups_disabled && (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT)) {
			em_udivconst_recipe recipe;
			em_sdivconst_recipe signed_recipe;

			/* All unsigned32 recipes fit <=12 baseline instructions, including
			 * normalization, two-word constant construction and remainder. */
			if (em_udivconst(c, ty, op, b.integer, &recipe) && a64_udivconst_ok(e, a, &recipe))
				return a64_udivconst(e, a, &recipe);
			if (em_sdivconst(c, ty, op, b.integer, &signed_recipe) && a64_sdivconst_ok(e, a, &signed_recipe))
				return a64_sdivconst(e, a, &signed_recipe);
		}
		{
			em_sdivpow2_recipe recipe;

			if (em_sdivpow2(c, ty, op, b.integer, &recipe))
				return a64_sdivpow2(e, a, &recipe);
		}
		em_divpow2(c, ty, &op, &b.integer);
	}
	ra = toreg(e, &a);
	if (b.kind == CODEGEN_VALUE_CONSTANT) {
		int64_t k = (int64_t)b.integer;
		uint64_t m = x ? (uint64_t)k : (uint32_t)k;

		switch (op) {
		case LEX_TOKEN_PLUS:
			opi(e, I_ADDI, x, ra, ra, x ? k : (int32_t)k);
			return a;
		case LEX_TOKEN_MINUS:
			opi(e, I_SUBI, x, ra, ra, x ? k : (int32_t)k);
			return a;
		case LEX_TOKEN_STAR:
			if (a64_mulconst(e, ty, ra, b.integer))
				return a;
			if (m && !(m & (m - 1))) {
				shift_imm(e, LEX_TOKEN_LESS_LESS, x, ra, ra, __builtin_ctzll(m));
				return a;
			}
			break;
		case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_PIPE: case LEX_TOKEN_CARET:
			if (logimm_ok(m, x)) {
				opi(e, op == LEX_TOKEN_AMPERSAND ? I_ANDI : op == LEX_TOKEN_PIPE ? I_ORRI : I_EORI, x, ra, ra, (int64_t)m);
				return a;
			}
			if (op == LEX_TOKEN_AMPERSAND && m == 0) {
				movi(e, x, ra, 0);
				return a;
			}
			if ((op == LEX_TOKEN_PIPE || op == LEX_TOKEN_CARET) && m == 0)
				return a;
			break;
		case LEX_TOKEN_LESS_LESS:
			shift_imm(e, LEX_TOKEN_LESS_LESS, x, ra, ra, (int)k);
			return a;
		case LEX_TOKEN_GREATER_GREATER:
			shift_imm(e, sg ? I_ASRV : I_LSRV, x, ra, ra, (int)k);
			return a;
		}
	}
	rb = toreg(e, &b);
	switch (op) {
	case LEX_TOKEN_PLUS: op3(e, I_ADD, x, ra, ra, rb); break;
	case LEX_TOKEN_MINUS: op3(e, I_SUB, x, ra, ra, rb); break;
	case LEX_TOKEN_STAR: op4(e, I_MADD, x, ra, ra, rb, ZR); break;
	case LEX_TOKEN_SLASH: op3(e, sg ? I_SDIV : I_UDIV, x, ra, ra, rb); break;
	case LEX_TOKEN_PERCENT:
		op3(e, sg ? I_SDIV : I_UDIV, x, R_X16, ra, rb);
		op4(e, I_MSUB, x, ra, R_X16, rb, ra);
		break;
	case LEX_TOKEN_AMPERSAND: op3(e, I_AND, x, ra, ra, rb); break;
	case LEX_TOKEN_PIPE: op3(e, I_ORR, x, ra, ra, rb); break;
	case LEX_TOKEN_CARET: op3(e, I_EOR, x, ra, ra, rb); break;
	case LEX_TOKEN_LESS_LESS: op3(e, I_LSLV, x, ra, ra, rb); break;
	case LEX_TOKEN_GREATER_GREATER: op3(e, sg ? I_ASRV : I_LSRV, x, ra, ra, rb); break;
	default:
		codegen_fatal_report(e, 0, "unsupported operator %d", op);
	}
	tfree(e, rb);
	return a;
}

/* 128-bit arithmetic on register pairs */
VPRIV codegen_value arith2(codegen_context *e, int op, ast_qualified_type ty, codegen_value a, codegen_value b)
{
	cc *c = e->compile_context;
	bool sg = codegen_type_is_signed(c, ty);

	if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER) {
		if (b.kind == CODEGEN_VALUE_CONSTANT) {
			int k = (int)(b.integer & 127);

			toreg2(e, &a, sg);
			if (k == 0)
				return a;
			if (op == LEX_TOKEN_LESS_LESS) {
				if (k >= 64) {
					shift_imm(e, LEX_TOKEN_LESS_LESS, 1, a.register_high, a.register_number, k - 64);
					movi(e, 1, a.register_number, 0);
				} else {
					shift_imm(e, LEX_TOKEN_LESS_LESS, 1, a.register_high, a.register_high, k);
					shift_imm(e, I_LSRV, 1, R_X16, a.register_number, 64 - k);
					op3(e, I_ORR, 1, a.register_high, a.register_high, R_X16);
					shift_imm(e, LEX_TOKEN_LESS_LESS, 1, a.register_number, a.register_number, k);
				}
			} else {
				if (k >= 64) {
					shift_imm(e, sg ? I_ASRV : I_LSRV, 1, a.register_number, a.register_high, k - 64);
					if (sg)
						shift_imm(e, I_ASRV, 1, a.register_high, a.register_high, 63);
					else
						movi(e, 1, a.register_high, 0);
				} else {
					shift_imm(e, I_LSRV, 1, a.register_number, a.register_number, k);
					shift_imm(e, LEX_TOKEN_LESS_LESS, 1, R_X16, a.register_high, 64 - k);
					op3(e, I_ORR, 1, a.register_number, a.register_number, R_X16);
					shift_imm(e, sg ? I_ASRV : I_LSRV, 1, a.register_high, a.register_high, k);
				}
			}
			return a;
		}
		{
			/* variable shift: both halves, then select on bit 6 of the count */
			int n, t1, t2, t3;

			toreg2(e, &a, sg);
			n = toreg(e, &b);
			t1 = treg(e), t2 = treg(e), t3 = treg(e);
			if (op == LEX_TOKEN_LESS_LESS) {
				shift_imm(e, I_LSRV, 1, t1, a.register_number, 1);
				op3(e, I_ORN, 0, t2, ZR, n);
				op3(e, I_LSLV, 1, t3, a.register_high, n);
				op3(e, I_LSRV, 1, t1, t1, t2);
				op3(e, I_ORR, 1, t1, t3, t1);		/* high, n < 64 */
				op3(e, I_LSLV, 1, t2, a.register_number, n);		/* low << n */
				opi(e, I_ANDSI, 1, ZR, n, 64);
				op3s(e, I_CSEL, 1, a.register_high, t2, t1, C_NE, 0);
				op3s(e, I_CSEL, 1, a.register_number, ZR, t2, C_NE, 0);
			} else {
				shift_imm(e, LEX_TOKEN_LESS_LESS, 1, t1, a.register_high, 1);
				op3(e, I_ORN, 0, t2, ZR, n);
				op3(e, I_LSRV, 1, t3, a.register_number, n);
				op3(e, I_LSLV, 1, t1, t1, t2);
				op3(e, I_ORR, 1, t1, t1, t3);		/* low, n < 64 */
				op3(e, sg ? I_ASRV : I_LSRV, 1, t2, a.register_high, n);	/* high >> n */
				if (sg)
					shift_imm(e, I_ASRV, 1, t3, a.register_high, 63);
				opi(e, I_ANDSI, 1, ZR, n, 64);
				op3s(e, I_CSEL, 1, a.register_number, t2, t1, C_NE, 0);
				op3s(e, I_CSEL, 1, a.register_high, sg ? t3 : ZR, t2, C_NE, 0);
			}
			tfree(e, t1), tfree(e, t2), tfree(e, t3);
			val_free(e, &b);
			return a;
		}
	}
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT) {
		codegen_value args[2] = { a, b };
		ast_qualified_type tys[2] = { ty, ty };
		int fn = op == LEX_TOKEN_SLASH ? (sg ? CODEGEN_LIBRARY_DIVTI3 : CODEGEN_LIBRARY_UDIVTI3) : (sg ? CODEGEN_LIBRARY_MODTI3 : CODEGEN_LIBRARY_UMODTI3);

		return call_named(e, CODEGEN_FIXED_REFERENCE(fn), args, tys, 2, ty, false);
	}
	toreg2(e, &a, sg);
	toreg2(e, &b, sg);
	switch (op) {
	case LEX_TOKEN_PLUS:
		op3(e, I_ADDS, 1, a.register_number, a.register_number, b.register_number);
		op3(e, I_ADC, 1, a.register_high, a.register_high, b.register_high);
		break;
	case LEX_TOKEN_MINUS:
		op3(e, I_SUBS, 1, a.register_number, a.register_number, b.register_number);
		op3(e, I_SBC, 1, a.register_high, a.register_high, b.register_high);
		break;
	case LEX_TOKEN_AMPERSAND: op3(e, I_AND, 1, a.register_number, a.register_number, b.register_number); op3(e, I_AND, 1, a.register_high, a.register_high, b.register_high); break;
	case LEX_TOKEN_PIPE: op3(e, I_ORR, 1, a.register_number, a.register_number, b.register_number); op3(e, I_ORR, 1, a.register_high, a.register_high, b.register_high); break;
	case LEX_TOKEN_CARET: op3(e, I_EOR, 1, a.register_number, a.register_number, b.register_number); op3(e, I_EOR, 1, a.register_high, a.register_high, b.register_high); break;
	case LEX_TOKEN_STAR:
		/* hi = umulh(alo, blo) + alo * bhi + ahi * blo; lo = alo * blo */
		op3(e, I_UMULH, 1, R_X16, a.register_number, b.register_number);
		op4(e, I_MADD, 1, R_X16, a.register_number, b.register_high, R_X16);
		op4(e, I_MADD, 1, a.register_high, a.register_high, b.register_number, R_X16);
		op4(e, I_MADD, 1, a.register_number, a.register_number, b.register_number, ZR);
		break;
	default:
		codegen_fatal_report(e, 0, "unsupported 128-bit operator %d", op);
	}
	val_free(e, &b);
	return a;
}

/* The top storage limb is extended from the declared precision. */
static void a64_bignorm(codegen_context *e, ast_qualified_type t, int r)
{
	unsigned bits = T(e->compile_context, t)->n & 63;
	if (bits) opbf(e, codegen_type_is_signed(e->compile_context, t) ? I_SBFM : I_UBFM, 1, r, r, 0, bits - 1);
}

static codegen_value a64_bigslot(codegen_context *e, int64_t sl)
{
	codegen_lvalue l = { .kind = CODEGEN_LVALUE_FRAME, .offset = sl };
	return laddr(e, &l);
}

VPRIV codegen_value a64_bigconst(codegen_context *e, ast_qualified_type t, const uint32_t *words, i128 small)
{
	unsigned count = (unsigned)(codegen_type_size(e->compile_context, t) / 8);
	int64_t sl = slot(e, count * 8, 8);
	int r = treg(e);
	for (unsigned i = 0; i < count; i++) {
		uint64_t bits = words ? (uint64_t)words[2*i] | (uint64_t)words[2*i+1] << 32 :
      i < 2 ? (uint64_t)((u128)small >> (64*i)) : small < 0 ? UINT64_MAX : 0;
		movi(e, 1, r, (int64_t)bits);
		if (i + 1 == count) a64_bignorm(e, t, r);
		mem(e, I_ST, 3, 0, r, FRAME, sl + 8*i);
	}
	tfree(e, r);
	return a64_bigslot(e, sl);
}

VPRIV codegen_value a64_bigcopy(codegen_context *e, codegen_value v, ast_qualified_type t)
{
	unsigned count = (unsigned)(codegen_type_size(e->compile_context, t) / 8);
	int64_t sl = slot(e, count * 8, 8);
	int p = toreg(e, &v), r = treg(e);
	for (unsigned i = 0; i < count; i++) {
		mem(e, I_LD, 3, 0, r, p, 8*i);
		if (i + 1 == count) a64_bignorm(e, t, r);
		mem(e, I_ST, 3, 0, r, FRAME, sl + 8*i);
	}
	tfree(e, r); val_free(e, &v);
	return a64_bigslot(e, sl);
}

VPRIV codegen_value a64_bigcvt(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;
	bool src = codegen_type_is_bitint_big(c, from), dst = codegen_type_is_bitint_big(c, to);
	if (src && AST_QUALIFIED_TYPE_UNQUALIFIED(from) == AST_QUALIFIED_TYPE_UNQUALIFIED(to)) return v;
	if (!dst) {
		int p = toreg(e, &v), lo = treg(e);
		mem(e, I_LD, 3, 0, lo, p, 0);
		if (codegen_type_is_wide(c, to)) {
			int hi = treg(e);
			mem(e, I_LD, 3, 0, hi, p, 8);
			val_free(e, &v);
			return (codegen_value){ .kind = CODEGEN_VALUE_REGISTER_PAIR, .register_number = (uint8_t)lo, .register_high = (uint8_t)hi };
		}
		val_free(e, &v);
		if (codegen_type_size(c, to) < 8) extend(e, lo, lo, (int)codegen_type_size(c, to), codegen_type_is_signed(c, to));
		return codegen_value_register(lo);
	}
	if (!src && v.kind == CODEGEN_VALUE_CONSTANT && (codegen_type_is_signed(c, from) || v.integer >= 0))
		return a64_bigconst(e, to, NULL, codegen_integer_truncate(c, v.integer, from));
	unsigned count = (unsigned)(codegen_type_size(c, to) / 8);
	unsigned source = src ? (unsigned)(codegen_type_size(c, from) / 8) : codegen_type_is_wide(c, from) ? 2 : 1;
	int64_t sl = slot(e, count * 8, 8);
	int value = treg(e), fill = treg(e);
	if (src) {
		toreg(e, &v);
		mem(e, I_LD, 3, 0, fill, v.register_number, 8*(source-1));
	} else {
		if (source == 2) toreg2(e, &v, codegen_type_is_signed(c, from)); else toreg(e, &v);
		mov(e, 1, fill, source == 2 ? v.register_high : v.register_number);
		if (codegen_type_size(c, from) < 8) extend(e, fill, fill, (int)codegen_type_size(c, from), codegen_type_is_signed(c, from));
	}
	if (codegen_type_is_signed(c, from)) shift_imm(e, I_ASRV, 1, fill, fill, 63); else movi(e, 1, fill, 0);
	for (unsigned i = 0; i < count; i++) {
		if (i >= source) mov(e, 1, value, fill);
		else if (src) mem(e, I_LD, 3, 0, value, v.register_number, 8*i);
		else {
			mov(e, 1, value, i ? v.register_high : v.register_number);
			if (!i && codegen_type_size(c, from) < 8) extend(e, value, value, (int)codegen_type_size(c, from), codegen_type_is_signed(c, from));
		}
		if (i + 1 == count) a64_bignorm(e, to, value);
		mem(e, I_ST, 3, 0, value, FRAME, sl + 8*i);
	}
	tfree(e, value); tfree(e, fill); val_free(e, &v);
	return a64_bigslot(e, sl);
}

VPRIV codegen_value a64_bigbool(codegen_context *e, codegen_value v, ast_qualified_type t)
{
	int p = toreg(e, &v), r = treg(e), limb = treg(e);
	movi(e, 1, r, 0);
	for (uint64_t i = 0; i < codegen_type_size(e->compile_context, t); i += 8) {
		mem(e, I_LD, 3, 0, limb, p, (int64_t)i);
		op3(e, I_ORR, 1, r, r, limb);
	}
	op3(e, I_SUBS, 1, ZR, r, ZR);
	cset(e, r, C_NE);
	tfree(e, limb); val_free(e, &v);
	return codegen_value_register(r);
}

VPRIV codegen_value a64_bigcmp(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	if (a.kind == CODEGEN_VALUE_CONSTANT) a = a64_bigconst(e, t, NULL, a.integer);
	if (b.kind == CODEGEN_VALUE_CONSTANT) b = a64_bigconst(e, t, NULL, b.integer);
	int pa = toreg(e, &a), pb = toreg(e, &b), left = treg(e), right = treg(e), result = treg(e);
	unsigned count = (unsigned)(codegen_type_size(e->compile_context, t) / 8);
	uint32_t done = newlabel(e);
	for (unsigned i = count; i-- > 0;) {
		mem(e, I_LD, 3, 0, left, pa, 8*i);
		mem(e, I_LD, 3, 0, right, pb, 8*i);
		op3(e, I_SUBS, 1, ZR, left, right);
		cset(e, result, cmp_cond(op, i + 1 == count && codegen_type_is_signed(e->compile_context, t)));
		if (i) bcond(e, C_NE, done);
	}
	deflabel(e, done);
	tfree(e, left); tfree(e, right); val_free(e, &a); val_free(e, &b);
	return codegen_value_register(result);
}

/* Read a limb at a runtime index. Out-of-range limbs are zero, or the
	* sign extension for an arithmetic right shift. Unsigned bounds also catch
	* negative indices, without ever forming an out-of-bounds address. */
static void a64_bigword(codegen_context *e, int rd, int ptr, unsigned n, int index, bool sign)
{
	uint32_t done = newlabel(e);
	if (sign) {
		mem(e, I_LD, 3, 0, rd, ptr, 8*(n-1));
		shift_imm(e, I_ASRV, 1, rd, rd, 63);
	} else movi(e, 1, rd, 0);
	opi(e, I_SUBSI, 1, ZR, index, n);
	bcond(e, C_HS, done);
	int addr = treg(e);
	shift_imm(e, LEX_TOKEN_LESS_LESS, 1, addr, index, 3);
	op3(e, I_ADD, 1, addr, ptr, addr);
	mem(e, I_LD, 3, 0, rd, addr, 0);
	tfree(e, addr);
	deflabel(e, done);
}

static codegen_value a64_bigshift(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	unsigned n = (unsigned)codegen_type_size(e->compile_context, t)/8;
	bool right = op == LEX_TOKEN_GREATER_GREATER, sign = right && codegen_type_is_signed(e->compile_context, t);
	int64_t sl = slot(e, n*8, 8);
	int pa = toreg(e, &a), count = toreg(e, &b);
	int whole = treg(e), bits = treg(e), complement = treg(e);
	shift_imm(e, I_LSRV, 1, whole, count, 6);
	opbf(e, I_UBFM, 1, bits, count, 0, 5);
	movi(e, 1, complement, 64);
	op3(e, I_SUB, 1, complement, complement, bits);
	int index = treg(e), value = treg(e), adjacent = treg(e);
	for (unsigned i = 0; i < n; i++) {
		uint32_t done = newlabel(e);
		movi(e, 1, index, i);
		op3(e, right ? I_ADD : I_SUB, 1, index, index, whole);
		a64_bigword(e, value, pa, n, index, sign);
		op3(e, right ? I_LSRV : I_LSLV, 1, value, value, bits);
		op3(e, I_SUBS, 1, ZR, bits, ZR);
		bcond(e, C_EQ, done);
		opi(e, right ? I_ADDI : I_SUBI, 1, index, index, 1);
		a64_bigword(e, adjacent, pa, n, index, sign);
		op3(e, right ? I_LSLV : I_LSRV, 1, adjacent, adjacent, complement);
		op3(e, I_ORR, 1, value, value, adjacent);
		deflabel(e, done);
		if (i + 1 == n) a64_bignorm(e, t, value);
		mem(e, I_ST, 3, 0, value, FRAME, sl+8*i);
	}
	tfree(e, whole); tfree(e, bits); tfree(e, complement);
	tfree(e, index); tfree(e, value); tfree(e, adjacent);
	val_free(e, &a); val_free(e, &b);
	return a64_bigslot(e, sl);
}

/* Negate a private limb object when sign is nonzero. Loads and stores do
	* not alter NZCV, so subtraction borrow travels across every limb. */
static void a64_bigneg_if(codegen_context *e, int ptr, unsigned n, int sign)
{
	uint32_t done = newlabel(e);
	op3(e, I_SUBS, 1, ZR, sign, ZR);
	bcond(e, C_EQ, done);
	int r = treg(e);
	for (unsigned i = 0; i < n; i++) {
		mem(e, I_LD, 3, 0, r, ptr, 8*i);
		op3(e, i ? I_SBCS : I_SUBS, 1, r, ZR, r);
		mem(e, I_ST, 3, 0, r, ptr, 8*i);
	}
	tfree(e, r);
	deflabel(e, done);
}

/* Restoring unsigned division of magnitudes. The extra remainder limb
	* retains the carry when the divisor occupies the full storage precision. */
static codegen_value a64_bigdiv(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	unsigned n = (unsigned)codegen_type_size(e->compile_context, t)/8;
	a = a64_bigcopy(e, a, t);
	b = a64_bigcopy(e, b, t);
	int q = toreg(e, &a), d = toreg(e, &b);
	int64_t sl = slot(e, 8*(n+1), 8);
	codegen_value remval = a64_bigslot(e, sl);
	int rem = remval.register_number;
	for (unsigned i = 0; i <= n; i++) mem(e, I_ST, 3, 0, ZR, rem, 8*i);
	int sa = treg(e), sb = treg(e);
	if (codegen_type_is_signed(e->compile_context, t)) {
		mem(e, I_LD, 3, 0, sa, q, 8*(n-1));
		mem(e, I_LD, 3, 0, sb, d, 8*(n-1));
		shift_imm(e, I_ASRV, 1, sa, sa, 63);
		shift_imm(e, I_ASRV, 1, sb, sb, 63);
		a64_bigneg_if(e, q, n, sa);
		a64_bigneg_if(e, d, n, sb);
	}
	int r = treg(e), divisor = treg(e), count = treg(e);
	uint32_t loop = newlabel(e), less = newlabel(e);
	movi(e, 1, count, 64*n);
	deflabel(e, loop);
	for (unsigned i = 0; i < n; i++) {
		mem(e, I_LD, 3, 0, r, q, 8*i);
		op3(e, i ? I_ADCS : I_ADDS, 1, r, r, r);
		mem(e, I_ST, 3, 0, r, q, 8*i);
	}
	for (unsigned i = 0; i <= n; i++) {
		mem(e, I_LD, 3, 0, r, rem, 8*i);
		op3(e, I_ADCS, 1, r, r, r);
		mem(e, I_ST, 3, 0, r, rem, 8*i);
	}
	/* Compare with a borrow chain, then subtract only if rem >= d. */
	for (unsigned pass = 0; pass < 2; pass++) {
		for (unsigned i = 0; i <= n; i++) {
			mem(e, I_LD, 3, 0, r, rem, 8*i);
			if (i < n) mem(e, I_LD, 3, 0, divisor, d, 8*i);
			else movi(e, 1, divisor, 0);
			op3(e, i ? I_SBCS : I_SUBS, 1, r, r, divisor);
			if (pass) mem(e, I_ST, 3, 0, r, rem, 8*i);
		}
		if (!pass) bcond(e, C_LO, less);
	}
	mem(e, I_LD, 3, 0, r, q, 0);
	opi(e, I_ADDI, 1, r, r, 1); /* quotient low bit is zero after shift */
	mem(e, I_ST, 3, 0, r, q, 0);
	deflabel(e, less);
	opi(e, I_SUBSI, 1, count, count, 1);
	bcond(e, C_NE, loop);
	int result = op == LEX_TOKEN_SLASH ? q : rem;
	if (codegen_type_is_signed(e->compile_context, t)) {
		if (op == LEX_TOKEN_SLASH) op3(e, I_EOR, 1, sa, sa, sb);
		a64_bigneg_if(e, result, n, sa);
	}
	mem(e, I_LD, 3, 0, r, result, 8*(n-1));
	a64_bignorm(e, t, r);
	mem(e, I_ST, 3, 0, r, result, 8*(n-1));
	tfree(e, op == LEX_TOKEN_SLASH ? rem : q); tfree(e, d);
	tfree(e, r); tfree(e, divisor); tfree(e, count); tfree(e, sa); tfree(e, sb);
	return codegen_value_register(result);
}

VPRIV codegen_value a64_bigbin(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	if (a.kind == CODEGEN_VALUE_CONSTANT) a = a64_bigconst(e, t, NULL, a.integer);
	if (op == LEX_TOKEN_LESS_LESS || op == LEX_TOKEN_GREATER_GREATER)
		return a64_bigshift(e, op, t, a, b);
	if (b.kind == CODEGEN_VALUE_CONSTANT) b = a64_bigconst(e, t, NULL, b.integer);
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT) return a64_bigdiv(e, op, t, a, b);
	unsigned count = (unsigned)(codegen_type_size(e->compile_context, t) / 8);
	int64_t sl = slot(e, count * 8, 8);
	int pa = toreg(e, &a), pb = toreg(e, &b), left = treg(e), right = treg(e);
	if (op == LEX_TOKEN_STAR) {
		/* Base 2^32 schoolbook multiplication: product + destination + carry
			* fits exactly in 64 bits. Discard terms above the result precision. */
		int carry = treg(e), acc = treg(e);
		for (unsigned i = 0; i < count; i++) mem(e, I_ST, 3, 0, ZR, FRAME, sl+8*i);
		for (unsigned i = 0; i < 2*count; i++) {
			movi(e, 1, carry, 0);
			mem(e, I_LD, 2, 0, left, pa, 4*i);
			for (unsigned j = 0; j < 2*count-i; j++) {
				mem(e, I_LD, 2, 0, right, pb, 4*j);
				mem(e, I_LD, 2, 0, acc, FRAME, sl+4*(i+j));
				op4(e, I_MADD, 1, right, left, right, acc);
				op3(e, I_ADD, 1, right, right, carry);
				mem(e, I_ST, 2, 0, right, FRAME, sl+4*(i+j));
				shift_imm(e, I_LSRV, 1, carry, right, 32);
			}
		}
		tfree(e, carry); tfree(e, acc);
		mem(e, I_LD, 3, 0, left, FRAME, sl+8*(count-1));
		a64_bignorm(e, t, left);
		mem(e, I_ST, 3, 0, left, FRAME, sl+8*(count-1));
	} else for (unsigned i = 0; i < count; i++) {
		mem(e, I_LD, 3, 0, left, pa, 8*i);
		mem(e, I_LD, 3, 0, right, pb, 8*i);
		int insn = op == LEX_TOKEN_PLUS ? (i ? I_ADCS : I_ADDS) : op == LEX_TOKEN_MINUS ? (i ? I_SBCS : I_SUBS) :
      op == LEX_TOKEN_AMPERSAND ? I_AND : op == LEX_TOKEN_PIPE ? I_ORR : op == LEX_TOKEN_CARET ? I_EOR : 0;
		if (!insn) codegen_fatal_report(e, 0, "arbitrary-width ARM integer operator is not supported yet");
		op3(e, insn, 1, left, left, right);
		if (i + 1 == count) a64_bignorm(e, t, left);
		mem(e, I_ST, 3, 0, left, FRAME, sl + 8*i);
	}
	tfree(e, left); tfree(e, right); val_free(e, &a); val_free(e, &b);
	return a64_bigslot(e, sl);
}
