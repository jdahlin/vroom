/* vcc code generator: builtins */
#include "../../../codegen/codegen-internal.h"

/* ---------------- builtins ---------------- */

/* evaluate n without emitting anything: constant or address constant? */
/*
 * __builtin_constant_p: generate the operand as if it were reachable (so
 * inlined bodies run their returns and fold), then throw the code away.
 * References made meanwhile do not count: nothing gets queued for emission.
 */
VPRIV codegen_value dry(codegen_context *e, uint32_t n)
{
	uint32_t nin = e->record_count, live = e->temporaries_in_use, usedcs = e->callee_saved_used;
	size_t raw = e->raw.n;
	int64_t foff = e->frame_offset;
	bool reach = e->is_reachable, calls = e->has_calls;
	int nclean = e->cleanup_count, nloop = e->loop_count;
	uint32_t nvsz = e->vla_size_count;
	uint8_t nkv = e->known_value_count;
	uint16_t kvalid = e->known_values_valid;
	int64_t kval[CODEGEN_KNOWN_VALUE_MAX];
	codegen_value v;

	memcpy(kval, e->known_values, sizeof(kval));
	e->dry_run_depth++;
	v = gen(e, n);
	e->dry_run_depth--;
	e->known_value_count = nkv, e->known_values_valid = kvalid;
	while (e->array_element_count && e->array_elements[e->array_element_count - 1].known_value_index >= nkv)
		e->array_element_count--;
	memcpy(e->known_values, kval, sizeof(kval));
	e->record_count = nin, e->temporaries_in_use = live, e->callee_saved_used = usedcs, e->frame_offset = foff;	/* labels stay: user labels may map to them */
	while (e->debug_row_count && e->debug_rows[e->debug_row_count - 1].record_index > e->record_count)
		e->debug_row_count--;
	e->is_reachable = reach, e->cleanup_count = nclean, e->loop_count = nloop, e->raw.n = raw, e->has_calls = calls, e->vla_size_count = nvsz;
	if (v.kind != CODEGEN_VALUE_CONSTANT && v.kind != CODEGEN_VALUE_ADDRESS)
		v = codegen_value_none();
	return v;
}

static codegen_value popcount(codegen_context *e, codegen_value v, int x)
{
	int r = toreg(e, &v), t = treg(e);
	int w = x ? 1 : 0;

	if (!x)
		op3(e, I_MOV, 0, r, r, 0);	/* zero-extend */
	/* v - ((v >> 1) & 0x55..) */
	shift_imm(e, I_LSRV, 1, t, r, 1);
	opi(e, I_ANDI, 1, t, t, 0x5555555555555555LL);
	op3(e, I_SUB, 1, r, r, t);
	/* (v & 0x33..) + ((v >> 2) & 0x33..) */
	shift_imm(e, I_LSRV, 1, t, r, 2);
	opi(e, I_ANDI, 1, t, t, 0x3333333333333333LL);
	opi(e, I_ANDI, 1, r, r, 0x3333333333333333LL);
	op3(e, I_ADD, 1, r, r, t);
	/* (v + (v >> 4)) & 0x0f.. */
	shift_imm(e, I_LSRV, 1, t, r, 4);
	op3(e, I_ADD, 1, r, r, t);
	opi(e, I_ANDI, 1, r, r, 0x0f0f0f0f0f0f0f0fLL);
	/* (v * 0x0101..) >> 56 */
	movi(e, 1, t, 0x0101010101010101LL);
	op4(e, I_MADD, 1, r, r, t, ZR);
	shift_imm(e, I_LSRV, 1, r, r, 56);
	tfree(e, t);
	(void)w;
	return v;
}

/* Extend using source precision, not BitInt's padded storage width. */
static void overflow_wide_operand(codegen_context *e, uint32_t n, int64_t sl)
{
	cc *c = e->compile_context;
	ast_qualified_type t = N(n)->ty;
	unsigned bits = codegen_type_is_bitint(c, t) ? T(c, t)->n : (unsigned)codegen_type_size(c, t) * 8;
	bool sign = codegen_type_is_signed(c, t);
	codegen_value v = gen(e, n);
	int ext = treg(e);

	toreg2(e, &v, sign);
	if (bits <= 64) {
		if (bits < 64) opbf(e, sign ? I_SBFM : I_UBFM, 1, v.register_number, v.register_number, 0, bits - 1);
		if (sign) shift_imm(e, I_ASRV, 1, v.register_high, v.register_number, 63);
		else movi(e, 1, v.register_high, 0);
	} else if (bits < 128) {
		opbf(e, sign ? I_SBFM : I_UBFM, 1, v.register_high, v.register_high, 0, bits - 65);
	}
	if (sign) shift_imm(e, I_ASRV, 1, ext, v.register_high, 63);
	else movi(e, 1, ext, 0);
	mem(e, I_ST, 3, 0, v.register_number, FRAME, sl);
	mem(e, I_ST, 3, 0, v.register_high, FRAME, sl + 8);
	mem(e, I_ST, 3, 0, ext, FRAME, sl + 16);
	mem(e, I_ST, 3, 0, ext, FRAME, sl + 24);
	tfree(e, ext);
	val_free(e, &v);
}

/* Exact 256-bit arithmetic.  The largest unsigned128 product is
 * 2^256-2^129+1, so it cannot wrap onto a representable <=128-bit result.
 * Signed products fit signed256.  No runtime helper is needed. */
static codegen_value gen_overflow_wide(codegen_context *e, const node *x, const uint32_t *arg)
{
	cc *c = e->compile_context;
	ast_qualified_type tr = T(c, N(arg[2])->ty)->base;
	unsigned bits = codegen_type_is_bitint(c, tr) ? T(c, tr)->n : (unsigned)codegen_type_size(c, tr) * 8;
	bool sign = codegen_type_is_signed(c, tr);
	int64_t as = slot(e, 32, 8), bs = slot(e, 32, 8), rs = slot(e, 32, 8), ps = slot(e, 8, 8);
	codegen_value p;
	int a, b, lo, hi, acc;

	overflow_wide_operand(e, arg[0], as);
	overflow_wide_operand(e, arg[1], bs);
	p = gen(e, arg[2]);
	mem(e, I_ST, 3, 0, toreg(e, &p), FRAME, ps);
	val_free(e, &p);
	a = treg(e), b = treg(e), lo = treg(e), hi = treg(e), acc = treg(e);
	if (x->aux != CC_BUILTIN_MUL_OVERFLOW) {
		bool add = x->aux == CC_BUILTIN_ADD_OVERFLOW;
		for (unsigned i = 0; i < 4; i++) {
			mem(e, I_LD, 3, 0, a, FRAME, as + i * 8);
			mem(e, I_LD, 3, 0, b, FRAME, bs + i * 8);
			op3(e, i == 0 ? (add ? I_ADDS : I_SUBS) : (add ? I_ADCS : I_SBCS), 1, lo, a, b);
			mem(e, I_ST, 3, 0, lo, FRAME, rs + i * 8);
		}
	} else {
		for (unsigned i = 0; i < 4; i++) mem(e, I_ST, 3, 0, ZR, FRAME, rs + i * 8);
		/* Accumulate four full 64x64 products, propagating every carry. */
		for (unsigned i = 0; i < 2; i++) for (unsigned j = 0; j < 2; j++) {
			unsigned k = i + j;
			mem(e, I_LD, 3, 0, a, FRAME, as + i * 8);
			mem(e, I_LD, 3, 0, b, FRAME, bs + j * 8);
			op3(e, I_UMULH, 1, hi, a, b);
			op4(e, I_MADD, 1, lo, a, b, ZR);
			mem(e, I_LD, 3, 0, acc, FRAME, rs + k * 8);
			op3(e, I_ADDS, 1, lo, acc, lo);
			mem(e, I_ST, 3, 0, lo, FRAME, rs + k * 8);
			mem(e, I_LD, 3, 0, acc, FRAME, rs + (k + 1) * 8);
			op3(e, I_ADCS, 1, hi, acc, hi);
			mem(e, I_ST, 3, 0, hi, FRAME, rs + (k + 1) * 8);
			for (unsigned h = k + 2; h < 4; h++) {
				mem(e, I_LD, 3, 0, acc, FRAME, rs + h * 8);
				op3(e, I_ADCS, 1, acc, acc, ZR);
				mem(e, I_ST, 3, 0, acc, FRAME, rs + h * 8);
			}
		}
		/* A = unsigned(A)-negative(A)*2^128.  Correct the product's high
		 * half for each negative factor, using its all-ones sign mask. */
		for (unsigned i = 0; i < 2; i++) {
			int64_t s = i ? bs : as, other = i ? as : bs;
			mem(e, I_LD, 3, 0, acc, FRAME, s + 16);
			mem(e, I_LD, 3, 0, a, FRAME, other);
			mem(e, I_LD, 3, 0, b, FRAME, other + 8);
			op3(e, I_AND, 1, a, a, acc);
			op3(e, I_AND, 1, b, b, acc);
			mem(e, I_LD, 3, 0, lo, FRAME, rs + 16);
			mem(e, I_LD, 3, 0, hi, FRAME, rs + 24);
			op3(e, I_SUBS, 1, lo, lo, a);
			op3(e, I_SBC, 1, hi, hi, b);
			mem(e, I_ST, 3, 0, lo, FRAME, rs + 16);
			mem(e, I_ST, 3, 0, hi, FRAME, rs + 24);
		}
	}
	/* Compare against the destination conversion extended back to 256 bits;
	 * keep that conversion for the builtin's mandatory result store. */
	mem(e, I_LD, 3, 0, lo, FRAME, rs);
	mem(e, I_LD, 3, 0, hi, FRAME, rs + 8);
	if (bits <= 64) {
		if (bits < 64) opbf(e, sign ? I_SBFM : I_UBFM, 1, lo, lo, 0, bits - 1);
		if (sign) shift_imm(e, I_ASRV, 1, hi, lo, 63);
		else movi(e, 1, hi, 0);
	} else if (bits < 128) {
		opbf(e, sign ? I_SBFM : I_UBFM, 1, hi, hi, 0, bits - 65);
	}
	if (sign) shift_imm(e, I_ASRV, 1, b, hi, 63);
	else movi(e, 1, b, 0);
	movi(e, 1, acc, 0);
	for (unsigned i = 0; i < 4; i++) {
		mem(e, I_LD, 3, 0, a, FRAME, rs + i * 8);
		op3(e, I_EOR, 1, a, a, i == 0 ? lo : i == 1 ? hi : b);
		op3(e, I_ORR, 1, acc, acc, a);
	}
	op3(e, I_SUBS, 1, ZR, acc, ZR);
	cset(e, acc, C_NE);
	tfree(e, a);
	tfree(e, b);
	mem(e, I_LD, 3, 0, a = treg(e), FRAME, ps);
	{
		codegen_lvalue l = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)a, .align = codegen_type_alignment(c, tr) };
		codegen_value r = { .kind = CODEGEN_VALUE_REGISTER_PAIR, .register_number = (uint8_t)lo, .register_high = (uint8_t)hi };
		store(e, &l, tr, &r);
		val_free(e, &r);
		lfree(e, &l);
	}
	return codegen_value_register(acc);
}

static codegen_value gen_overflow(codegen_context *e, const node *x, const uint32_t *arg)
{
	cc *c = e->compile_context;
	ast_qualified_type ta = N(arg[0])->ty, tb = N(arg[1])->ty, tr = T(c, N(arg[2])->ty)->base;
	if (codegen_type_size(c, ta) > 8 || codegen_type_size(c, tb) > 8 || (codegen_type_is_bitint(c, tr) && T(c, tr)->n > 64))
		return gen_overflow_wide(e, x, arg);
	codegen_value a = gen(e, arg[0]), b = gen(e, arg[1]), p = gen(e, arg[2]);
	int bits = codegen_type_is_bitint(c, tr) ? (int)T(c, tr)->n : (int)codegen_type_size(c, tr) * 8;
	bool sr = codegen_type_is_signed(c, tr);
	int lo, hi, t, ok;
	codegen_lvalue l;

	/* exact result in 128 bits */
	{
		int ra = toreg(e, &a), rb = toreg(e, &b);

		extend(e, ra, ra, (int)codegen_type_size(c, ta), codegen_type_is_signed(c, ta));
		extend(e, rb, rb, (int)codegen_type_size(c, tb), codegen_type_is_signed(c, tb));
		toreg2(e, &a, codegen_type_is_signed(c, ta));
		toreg2(e, &b, codegen_type_is_signed(c, tb));
	}
	lo = a.register_number, hi = a.register_high;
	switch (x->aux) {
	case CC_BUILTIN_ADD_OVERFLOW:
		op3(e, I_ADDS, 1, lo, lo, b.register_number);
		op3(e, I_ADC, 1, hi, hi, b.register_high);
		break;
	case CC_BUILTIN_SUB_OVERFLOW:
		op3(e, I_SUBS, 1, lo, lo, b.register_number);
		op3(e, I_SBC, 1, hi, hi, b.register_high);
		break;
	default:
		op3(e, I_UMULH, 1, R_X16, lo, b.register_number);
		op4(e, I_MADD, 1, R_X16, lo, b.register_high, R_X16);
		op4(e, I_MADD, 1, hi, hi, b.register_number, R_X16);
		op4(e, I_MADD, 1, lo, lo, b.register_number, ZR);
		break;
	}
	val_free(e, &b);
	if (bits == 128) {
		/*
		 * Only two unsigned factors can produce a positive result above
		 * signed 128-bit range.  Addition and subtraction fit signed 128,
		 * even for unsigned operands (subtraction can be negative).
		 */
		bool uu = x->aux == CC_BUILTIN_MUL_OVERFLOW && !codegen_type_is_signed(c, ta) && !codegen_type_is_signed(c, tb);
		codegen_value r = { .kind = CODEGEN_VALUE_REGISTER_PAIR, .register_number = (uint8_t)lo, .register_high = (uint8_t)hi };

		ok = treg(e);
		if (uu == sr)
			shift_imm(e, I_LSRV, 1, ok, hi, 63);
		else
			movi(e, 0, ok, 0);
		l = lfromval(e, p, codegen_type_alignment(c, tr));
		store(e, &l, tr, &r);
		lfree(e, &l);
		tfree(e, lo);
		tfree(e, hi);
		return codegen_value_register(ok);
	}
	/* fits iff ext(lo) == lo && hi == (signed ? lo >> 63 : 0) */
	t = treg(e);
	ok = treg(e);
	if (bits < 64) {
		opbf(e, sr ? I_SBFM : I_UBFM, 1, t, lo, 0, bits - 1);
		op3(e, I_SUBS, 1, ZR, t, lo);
		cset(e, ok, C_NE);
	} else {
		movi(e, 0, ok, 0);
	}
	if (sr || bits < 64)
		shift_imm(e, sr ? I_ASRV : I_LSRV, 1, t, lo, 63);
	if (sr)
		op3(e, I_SUBS, 1, ZR, hi, t);
	else
		op3(e, I_SUBS, 1, ZR, hi, ZR);
	/* ok |= (hi != expected) */
	cset(e, t, C_NE);
	op3(e, I_ORR, 0, ok, ok, t);
	tfree(e, t);
	l = lfromval(e, p, codegen_type_alignment(c, tr));
	{
		codegen_value r = codegen_value_register(lo);

		store(e, &l, tr, &r);
	}
	lfree(e, &l);
	tfree(e, lo);
	tfree(e, hi);
	return codegen_value_register(ok);
}

/* __builtin_addc* / __builtin_subc*: x +- y +- carry_in, *carry_out = the carry (borrow) of either step */
static codegen_value gen_carry(codegen_context *e, const node *x, const uint32_t *arg)
{
	cc *c = e->compile_context;
	ast_qualified_type ty = x->ty, tc = T(c, N(arg[3])->ty)->base;
	int sz = (int)codegen_type_size(c, ty), bits = sz * 8;
	bool add = x->aux == CC_BUILTIN_ADDC;
	codegen_value a = gen(e, arg[0]), b = gen(e, arg[1]), ci = gen(e, arg[2]), p = gen(e, arg[3]);
	int ra = toreg(e, &a), rb = toreg(e, &b), rc = toreg(e, &ci), co = treg(e);
	codegen_lvalue l;

	extend(e, ra, ra, sz, false);
	extend(e, rb, rb, sz, false);
	extend(e, rc, rc, sz, false);
	if (bits == 64) {
		int t = treg(e);

		op3(e, add ? I_ADDS : I_SUBS, 1, ra, ra, rb);
		cset(e, co, add ? C_HS : C_LO);
		op3(e, add ? I_ADDS : I_SUBS, 1, ra, ra, rc);
		cset(e, t, add ? C_HS : C_LO);
		op3(e, I_ORR, 0, co, co, t);
		tfree(e, t);
	} else {
		op3(e, add ? I_ADD : I_SUB, 1, ra, ra, rb);
		op3(e, add ? I_ADD : I_SUB, 1, ra, ra, rc);
		/* add: anything at or above bit 'bits' is the carry; sub: a negative result is the borrow */
		shift_imm(e, I_LSRV, 1, co, ra, add ? bits : 63);
		op3(e, I_SUBS, 1, ZR, co, ZR);
		cset(e, co, C_NE);
		opbf(e, I_UBFM, 1, ra, ra, 0, bits - 1);
	}
	val_free(e, &b);
	val_free(e, &ci);
	l = lfromval(e, p, codegen_type_alignment(c, tc));
	{
		codegen_value r = codegen_value_register(co);

		store(e, &l, tc, &r);
	}
	lfree(e, &l);
	tfree(e, co);
	return codegen_value_register(ra);
}

static void va_start_(codegen_context *e, uint32_t apn)
{
	codegen_lvalue l = gen_lval(e, apn);
	codegen_value a = laddr(e, &l);
	int ra = toreg(e, &a), t = treg(e);

	if (CODEGEN_IS_DARWIN(e->compile_context)) {
		/* char *: the first anonymous argument, after the named stack ones */
		opi(e, I_ADDI, 1, t, R_FP, 16 + ((e->named_stack_bytes + 7) & ~7));
		mem(e, I_ST, 3, 0, t, ra, 0);
		tfree(e, t);
		val_free(e, &a);
		return;
	}

	/* __stack */
	opi(e, I_ADDI, 1, t, R_FP, 16 + e->named_stack_bytes);
	mem(e, I_ST, 3, 0, t, ra, 0);
	/* __gr_top: end of the register save area */
	opi(e, I_ADDI, 1, t, FRAME, e->variadic_register_save_slot + 64);
	mem(e, I_ST, 3, 0, t, ra, 8);
	/* __vr_top, __gr_offs, __vr_offs */
	opi(e, I_ADDI, 1, t, FRAME, e->variadic_vector_save_slot + 128);
	mem(e, I_ST, 3, 0, t, ra, 16);
	movi(e, 0, t, -(8 - e->named_general_register_count) * 8);
	mem(e, I_ST, 2, 0, t, ra, 24);
	movi(e, 0, t, -(8 - e->named_vector_register_count) * 16);
	mem(e, I_ST, 2, 0, t, ra, 28);
	tfree(e, t);
	val_free(e, &a);
}

VPRIV codegen_value va_arg_(codegen_context *e, const node *x)
{
	cc *c = e->compile_context;
	ast_qualified_type t = x->ty;
	if (!CODEGEN_IS_DARWIN(c) && codegen_type_is_bool_vector(c, t) && (T(c, t)->n == 64 || T(c, t)->n == 128))
		codegen_fatal_report(e, e->current_location, "arm64: variadic transport of 64/128-lane packed boolean vectors is not supported");
	uint64_t size = codegen_type_size(c, t);
	bool big = codegen_type_is_record_like(c, t) && size > 16;
	int nreg = big ? 1 : (int)((size + 7) / 8);
	/* Empty aggregates consume neither registers nor stack alignment. */
	bool al16 = size != 0 && !big && abi_align(c, t) == 16;
	codegen_lvalue l = gen_lval(e, x->a);
	codegen_value a = laddr(e, &l);
	int ra = toreg(e, &a), off = treg(e), addr = treg(e);
	uint32_t lstack = newlabel(e), ldone = newlabel(e);

	if (CODEGEN_IS_DARWIN(c)) {
		mem(e, I_LD, 3, 0, addr, ra, 0);
		if (al16) {
			opi(e, I_ADDI, 1, addr, addr, 15);
			opi(e, I_ANDI, 1, addr, addr, -16);
		}
		opi(e, I_ADDI, 1, off, addr, big ? 8 : ((int64_t)size + 7) & ~7);
		mem(e, I_ST, 3, 0, off, ra, 0);
	} else {
	if (codegen_type_is_float(c, t)) {
		/* __vr_offs / __vr_top, 16 bytes a register */
		mem(e, I_LD, 2, 1, off, ra, 28);
		opi(e, I_SUBSI, 1, ZR, off, 0);
		bcond(e, C_GE, lstack);
		opi(e, I_ADDI, 1, addr, off, 16);
		mem(e, I_ST, 2, 0, addr, ra, 28);
		opi(e, I_SUBSI, 1, ZR, addr, 0);
		bcond(e, C_GT, lstack);
		mem(e, I_LD, 3, 0, addr, ra, 16);
		op3(e, I_ADD, 1, addr, addr, off);
		jump(e, ldone);
		nreg = size > 8 ? 2 : 1;
		al16 = size > 8;
		goto stack;
	}
	int hn, hb = hfa(c, t, &hn);

	if (hb) {
		/* an HFA: hn v registers, copied together into a temporary */
		int64_t sl = slot(e, 64, 16);

		mem(e, I_LD, 2, 1, off, ra, 28);
		opi(e, I_SUBSI, 1, ZR, off, 0);
		bcond(e, C_GE, lstack);
		opi(e, I_ADDI, 1, addr, off, 16 * hn);
		mem(e, I_ST, 2, 0, addr, ra, 28);
		opi(e, I_SUBSI, 1, ZR, addr, 0);
		bcond(e, C_GT, lstack);
		mem(e, I_LD, 3, 0, addr, ra, 16);
		op3(e, I_ADD, 1, addr, addr, off);
		for (int k = 0; k < hn; k++)
			for (int w = 0; w < hb; w += 8) {
				int lg = hb >= 8 ? 3 : 2;

				mem(e, I_LD, lg, 0, R_X16, addr, 16 * k + w);
				mem(e, I_ST, lg, 0, R_X16, FRAME, sl + hb * k + w);
			}
		opi(e, I_ADDI, 1, addr, FRAME, sl);
		jump(e, ldone);
		nreg = (int)((size + 7) / 8);
		big = false;
		al16 = abi_align(c, t) >= 16;
		goto stack;
	}
	mem(e, I_LD, 2, 1, off, ra, 24);
	opi(e, I_SUBSI, 1, ZR, off, 0);
	bcond(e, C_GE, lstack);
	if (al16) {
		opi(e, I_ADDI, 1, off, off, 15);
		opi(e, I_ANDI, 1, off, off, -16);
	}
	opi(e, I_ADDI, 1, addr, off, nreg * 8);
	mem(e, I_ST, 2, 0, addr, ra, 24);
	opi(e, I_SUBSI, 1, ZR, addr, 0);
	bcond(e, C_GT, lstack);
	mem(e, I_LD, 3, 0, addr, ra, 8);
	op3(e, I_ADD, 1, addr, addr, off);
	jump(e, ldone);
stack:
	deflabel(e, lstack);
	mem(e, I_LD, 3, 0, addr, ra, 0);
	if (al16) {
		opi(e, I_ADDI, 1, addr, addr, 15);
		opi(e, I_ANDI, 1, addr, addr, -16);
	}
	opi(e, I_ADDI, 1, off, addr, nreg * 8);
	mem(e, I_ST, 3, 0, off, ra, 0);
	deflabel(e, ldone);
	}
	tfree(e, off);
	val_free(e, &a);
	if (big)
		mem(e, I_LD, 3, 0, addr, addr, 0);
	{
		codegen_lvalue m = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)addr, .align = 8 };
		codegen_value v = load(e, &m, t);

		if (!(v.kind == CODEGEN_VALUE_REGISTER && v.register_number == addr))
			tfree(e, addr);
		return v;
	}
}

/*
 * Atomic read-modify-write instructions: the LSE one when the target has
 * LSE (-march=armv8.1-a and later, +lse, most -mcpu, Darwin), else the same
 * effect as an ldaxr/stlxr loop (what clang emits with -mno-outline-atomics).
 * Registers as in the LSE forms; sz is log2 of the size.
 */
static void ex_ld(codegen_context *e, unsigned sz, int rt, int rn)
{
	word(e, sz << 30 | 0x085ffc00u | (unsigned)rn << 5 | (unsigned)rt);	/* ldaxr */
}

static void ex_st(codegen_context *e, unsigned sz, int ws, int rt, int rn)
{
	word(e, sz << 30 | 0x0800fc00u | (unsigned)ws << 16 | (unsigned)rn << 5 | (unsigned)rt);	/* stlxr */
}

/* casal rs, rt, [rn]: rs = old (zero-extended); *rn = rt if old == rs */
VPRIV void at_cas(codegen_context *e, unsigned sz, int rs, int rt, int rn)
{
	int t, ws, cmpv;
	uint32_t top, fail;

	if (e->compile_context->o->lse) {
		word(e, sz << 30 | 0x08e0fc00u | (unsigned)rs << 16 | (unsigned)rn << 5 | (unsigned)rt);
		return;
	}
	t = treg(e), ws = treg(e), cmpv = treg(e);
	if (sz < 2)
		extend(e, cmpv, rs, 1 << sz, false);
	else
		mov(e, 1, cmpv, rs);
	top = newlabel(e), fail = newlabel(e);
	deflabel_loop(e, top, true);
	ex_ld(e, sz, t, rn);
	op3(e, I_SUBS, sz == 3, ZR, t, cmpv);
	bcond(e, C_NE, fail);
	ex_st(e, sz, ws, rt, rn);
	cbz(e, true, 0, ws, top);
	deflabel_forced(e, fail);
	word(e, 0xd503305fu);	/* clrex (harmless after a store) */
	mov(e, 1, rs, t);
	tfree(e, cmpv), tfree(e, ws), tfree(e, t);
}

/* swpal rs, rt, [rn]: rt = old, *rn = rs */
VPRIV void at_swp(codegen_context *e, unsigned sz, int rs, int rt, int rn)
{
	int t, ws;
	uint32_t top;

	if (e->compile_context->o->lse) {
		word(e, sz << 30 | 0x38e08000u | (unsigned)rs << 16 | (unsigned)rn << 5 | (unsigned)rt);
		return;
	}
	t = treg(e), ws = treg(e);
	top = newlabel(e);
	deflabel_loop(e, top, true);
	ex_ld(e, sz, t, rn);
	ex_st(e, sz, ws, rs, rn);
	cbz(e, true, 0, ws, top);
	mov(e, 1, rt, t);
	tfree(e, ws), tfree(e, t);
}

/* u = t op rs for the opc of at_ldop (min/max on the low 1 << sz bytes, signed or not) */
static void at_opval(codegen_context *e, unsigned sz, unsigned opc, int u, int t, int rs)
{
	static const int op[] = { I_ADD, I_BIC, I_EOR, I_ORR };
	static const int cond[] = { C_GT, C_LT, C_HI, C_LO };	/* smax smin umax umin: keep t */

	if (opc < 4) {
		op3(e, op[opc], sz == 3, u, t, rs);
	} else if (opc == 8) {	/* nand */
		op3(e, I_AND, 1, u, t, rs);
		op3(e, I_ORN, 1, u, ZR, u);
	} else {
		bool sg = opc < 6;
		int a = t, b = rs, w = -1;

		if (sz < 2) {
			w = treg(e);
			extend(e, u, t, 1 << sz, sg), extend(e, w, rs, 1 << sz, sg);
			a = u, b = w;
		}
		op3(e, I_SUBS, sz == 3, ZR, a, b);
		op3s(e, I_CSEL, 1, u, t, rs, cond[opc - 4], 0);
		if (w >= 0)
			tfree(e, w);
	}
}

/*
 * ld<op>al rs, rt, [rn]: rt = old, *rn = old op rs; opc: 0 add, 1 clr, 2 eor,
 * 3 set, 4 smax, 5 smin, 6 umax, 7 umin; 8 nand (no LSE form: always a loop)
 */
VPRIV void at_ldop(codegen_context *e, unsigned sz, unsigned opc, int rs, int rt, int rn)
{
	int t, u, ws;
	uint32_t top;

	if (e->compile_context->o->lse && opc < 8) {
		word(e, sz << 30 | 0x38e00000u | (unsigned)rs << 16 | opc << 12 | (unsigned)rn << 5 | (unsigned)rt);
		return;
	}
	t = treg(e), u = treg(e), ws = treg(e);
	top = newlabel(e);
	deflabel_loop(e, top, true);
	ex_ld(e, sz, t, rn);
	at_opval(e, sz, opc, u, t, rs);
	ex_st(e, sz, ws, u, rn);
	cbz(e, true, 0, ws, top);
	mov(e, 1, rt, t);
	tfree(e, ws), tfree(e, u), tfree(e, t);
}

/*
 * __atomic_*: LSE instructions, always acquire+release (ldar/stlr, swpal,
 * casal, ld<op>al), which is at least what any memory order asks for.
 */
static codegen_value gen_atomic(codegen_context *e, const node *x, const uint32_t *arg)
{
	cc *c = e->compile_context;
	int id = x->aux;
	ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(T(c, N(arg[0])->ty)->base);
	uint64_t n = codegen_type_size(c, t);
	unsigned sz = (unsigned)log2sz(n), xs = n == 8;
	bool sg = codegen_type_is_integer(c, t) && codegen_type_is_signed(c, t);
	codegen_value p = gen(e, arg[0]), v1 = codegen_value_none(), v2 = codegen_value_none(), res = codegen_value_none();
	int rp, nmo = id == CC_BUILTIN_ATOMIC_CMPXCHG_N || id == CC_BUILTIN_ATOMIC_CMPXCHG ? 2 : 1, nargs = (int)x->c;

	if (n == 16)
		codegen_fatal_report(e, x->loc, "16-byte atomics are not supported");
	rp = toreg(e, &p);
	/* operands, then the memory orders (evaluated for their side effects) */
	if (nargs - nmo > 1)
		v1 = gen(e, arg[1]);
	if (nargs - nmo > 2)
		v2 = gen(e, arg[2]);
	for (int k = nargs - nmo; k < nargs; k++) {
		codegen_value m = gen(e, arg[k]);

		val_free(e, &m);
	}
	if (nargs - nmo > 3) {	/* weak: ignored */
		codegen_value w = gen(e, arg[3]);

		val_free(e, &w);
	}
	switch (id) {
	case CC_BUILTIN_ATOMIC_LOAD_N: case CC_BUILTIN_ATOMIC_LOAD: {
		int r = treg(e);

		word(e, sz << 30 | 0x08dffc00u | (unsigned)rp << 5 | (unsigned)r);	/* ldar */
		if (id == CC_BUILTIN_ATOMIC_LOAD) {
			int rr = toreg(e, &v1);

			mem(e, I_ST, (int)sz, 0, r, rr, 0);
			tfree(e, r);
			break;
		}
		if (sg && n < 8)
			extend(e, r, r, (int)n, true);
		res = codegen_value_register(r);
		break;
	}
	case CC_BUILTIN_ATOMIC_STORE_N: case CC_BUILTIN_ATOMIC_STORE: {
		int r;

		if (id == CC_BUILTIN_ATOMIC_STORE) {
			int rv = toreg(e, &v1);

			r = treg(e);
			mem(e, I_LD, (int)sz, 0, r, rv, 0);
			tfree(e, r);
		}
		r = id == CC_BUILTIN_ATOMIC_STORE ? r : toreg(e, &v1);
		word(e, sz << 30 | 0x089ffc00u | (unsigned)rp << 5 | (unsigned)r);	/* stlr */
		break;
	}
	case CC_BUILTIN_ATOMIC_EXCHANGE_N: case CC_BUILTIN_ATOMIC_EXCHANGE: {
		int rs, r = treg(e);

		if (id == CC_BUILTIN_ATOMIC_EXCHANGE) {
			int rv = toreg(e, &v1);

			rs = treg(e);
			mem(e, I_LD, (int)sz, 0, rs, rv, 0);
		} else {
			rs = toreg(e, &v1);
		}
		at_swp(e, sz, rs, r, rp);	/* swpal */
		if (id == CC_BUILTIN_ATOMIC_EXCHANGE) {
			int rr = toreg(e, &v2);

			mem(e, I_ST, (int)sz, 0, r, rr, 0);
			tfree(e, rs);
			tfree(e, r);
			break;
		}
		if (sg && n < 8)
			extend(e, r, r, (int)n, true);
		res = codegen_value_register(r);
		break;
	}
	case CC_BUILTIN_ATOMIC_CMPXCHG_N: case CC_BUILTIN_ATOMIC_CMPXCHG: {
		/* old = *expected; casal old, desired, [p]; ok = old == *expected; if !ok *expected = old */
		int re = toreg(e, &v1), rd, old = treg(e), cmpv = treg(e), ok;
		uint32_t l = newlabel(e);

		if (id == CC_BUILTIN_ATOMIC_CMPXCHG) {
			int rv = toreg(e, &v2);

			rd = treg(e);
			mem(e, I_LD, (int)sz, 0, rd, rv, 0);
		} else {
			rd = toreg(e, &v2);
		}
		mem(e, I_LD, (int)sz, 0, old, re, 0);
		mov(e, 1, cmpv, old);
		at_cas(e, sz, old, rd, rp);	/* casal */
		if (n < 4)
			extend(e, cmpv, cmpv, (int)n, false), extend(e, old, old, (int)n, false);
		op3(e, I_SUBS, xs, ZR, old, cmpv);
		ok = treg(e);
		cset(e, ok, C_EQ);
		cbz(e, true, 0, ok, l);
		mem(e, I_ST, (int)sz, 0, old, re, 0);
		deflabel(e, l);
		tfree(e, old);
		tfree(e, cmpv);
		if (id == CC_BUILTIN_ATOMIC_CMPXCHG)
			tfree(e, rd);
		res = codegen_value_register(ok);
		break;
	}
	case CC_BUILTIN_SYNC_VAL_CAS: case CC_BUILTIN_SYNC_BOOL_CAS: {
		/* old = expected; casal old, desired, [p]; val: old; bool: old == expected (low n bytes) */
		int re = toreg(e, &v1), rd = toreg(e, &v2), old = treg(e);

		mov(e, 1, old, re);
		at_cas(e, sz, old, rd, rp);	/* casal */
		if (id == CC_BUILTIN_SYNC_VAL_CAS) {
			if (sg && n < 8)
				extend(e, old, old, (int)n, true);
			res = codegen_value_register(old);
			break;
		}
		if (n < 4) {
			int cmpv = treg(e);

			mov(e, 1, cmpv, re);
			extend(e, cmpv, cmpv, (int)n, false);
			op3(e, I_SUBS, 0, ZR, old, cmpv);	/* casal b/h zero-extends old */
			tfree(e, cmpv);
		} else {
			op3(e, I_SUBS, xs, ZR, old, re);
		}
		cset(e, old, C_EQ);
		res = codegen_value_register(old);
		break;
	}
	default: {
		/* ld<op>al: add 000, clr 001, eor 010, set 011, smax 100, smin 101, umax 110, umin 111 */
		int base = id >= CC_BUILTIN_ATOMIC_ADD_FETCH ? id - CC_BUILTIN_ATOMIC_ADD_FETCH : id - CC_BUILTIN_ATOMIC_FETCH_ADD;	/* add sub and or xor nand max min */
		static const unsigned opc[] = { 0, 0, 1, 3, 2, 8, 4, 5 };
		unsigned o = opc[base] + (base >= 6 && !sg ? 2 : 0);
		int rv = toreg(e, &v1), rs = treg(e), r = treg(e);

		if (base == 1)
			op3(e, I_SUB, 1, rs, ZR, rv);		/* neg */
		else if (base == 2)
			op3(e, I_ORN, 1, rs, ZR, rv);		/* mvn: clear the bits not in v */
		else
			mov(e, 1, rs, rv);
		at_ldop(e, sz, o, rs, r, rp);
		tfree(e, rs);
		if (id >= CC_BUILTIN_ATOMIC_ADD_FETCH) {
			static const int bop[] = { I_ADD, I_SUB, I_AND, I_ORR, I_EOR };

			if (base < 5) {
				op3(e, bop[base], 1, r, r, rv);
			} else {
				int u = treg(e);

				at_opval(e, sz, o, u, r, rv);
				mov(e, 1, r, u);
				tfree(e, u);
			}
		}
		if (n < 8)
			extend(e, r, r, (int)n, sg);
		res = codegen_value_register(r);
		break;
	}
	}
	val_free(e, &v1);
	val_free(e, &v2);
	val_free(e, &p);
	return res;
}

/* Preserve the existing front-end/literal/dry policy for both builtin values
 * and shared control proofs. dry() restores the target emission state. */
VPRIV int a64_constant_p(codegen_context *e, uint32_t arg)
{
	cc *c = e->compile_context;
	codegen_value v;
	/*
	 * clang: 1 if the front end folds the operand (a pointer only as a
	 * string literal at offset 0); else 0 if it has side effects or is
	 * not arithmetic; else whatever the optimizer folds, which here is
	 * the operand after inlining (dry).
	 */
	cc_value cv;
	ast_qualified_type at = N(arg)->ty;

	if (cc_eval(c, arg, &cv) && (cv.kind == CC_VALUE_INTEGER || cv.kind == CC_VALUE_FLOAT ||
					 (cv.kind == CC_VALUE_ADDRESS && cv.str && cv.i == 0 && !cv.sym && !cv.label)))
		return 1;
	if (codegen_expression_has_side_effects(c, arg) || !(codegen_type_is_integer(c, at) || codegen_type_is_float(c, at)))
		return 0;
	{
		/* "hi"[0]: the optimizer reads a character of a literal */
		uint32_t r = arg;
		cc_value ix;

		while (N(r)->op == AST_NODE_CAST && (N(r)->aux == AST_CAST_INTEGER || N(r)->aux == AST_CAST_NOP))
			r = N(r)->a;
		if (N(r)->op == AST_NODE_INDEX && cc_eval(c, N(r)->b, &ix) && ix.kind == CC_VALUE_INTEGER) {
			uint32_t s = N(r)->a;

			while (N(s)->op == AST_NODE_CAST && (N(s)->aux == AST_CAST_ARRAY_DECAY || N(s)->aux == AST_CAST_NOP))
				s = N(s)->a;
			if (N(s)->op == AST_NODE_STRING && ix.i >= 0 && (uint64_t)ix.i < c->strs[N(s)->a].n)
				return 1;
		}
	}
	v = dry(e, arg);
	return v.kind == CODEGEN_VALUE_CONSTANT;
}

VPRIV codegen_value gen_builtin(codegen_context *e, uint32_t n)
{
	cc *c = e->compile_context;
	const node *x = N(n);
	const uint32_t *arg = &c->lists[x->b];
	codegen_value v;

	switch (x->aux) {
	case CC_BUILTIN_SHUFFLEVECTOR: case CC_BUILTIN_CONVERTVECTOR: {
		/* lane by lane through a frame slot (vectors are addresses here) */
		ast_qualified_type at = N(arg[0])->ty, ae = T(c, at)->base, re = T(c, x->ty)->base;
		unsigned na = T(c, at)->ext ?: T(c, at)->n, nr = T(c, x->ty)->ext ?: T(c, x->ty)->n;
		codegen_value a = gen(e, arg[0]), b = x->aux == CC_BUILTIN_SHUFFLEVECTOR ? gen(e, arg[1]) : codegen_value_constant(0);
		int ra = toreg(e, &a), rb = x->aux == CC_BUILTIN_SHUFFLEVECTOR ? toreg(e, &b) : -1;
		int64_t sl = slot(e, codegen_type_size(c, x->ty), 16);
		int rd = treg(e);

		opi(e, I_ADDI, 1, rd, FRAME, sl);
		for (unsigned i = 0; i < nr; i++) {
			codegen_lvalue d = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)rd, .offset = (int64_t)i * (int64_t)codegen_type_size(c, re), .align = codegen_type_alignment(c, re) };
			codegen_value v;

			if (x->aux == CC_BUILTIN_SHUFFLEVECTOR) {
				int64_t k = (int64_t)c->ipool[N(arg[2 + i])->a];
				codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)(k < (int64_t)na ? ra : rb),
					   .offset = (k < (int64_t)na ? k : k - (int64_t)na) * (int64_t)codegen_type_size(c, ae), .align = codegen_type_alignment(c, ae) };

				if (codegen_type_is_bool_vector(c, at)) { int64_t j = k < (int64_t)na ? k : k - na; s.offset = j / 8, s.is_bitfield = 1, s.width = 1, s.bit_offset = j % 8; }
				v = k < 0 ? codegen_value_constant(0) : load(e, &s, ae);
			} else {
				codegen_lvalue s = { .kind = CODEGEN_LVALUE_REGISTER, .register_number = (uint8_t)ra, .offset = (int64_t)i * (int64_t)codegen_type_size(c, ae), .align = codegen_type_alignment(c, ae) };
				int ck = codegen_type_is_float(c, ae) ? (codegen_type_is_float(c, re) ? AST_CAST_FLOAT_TO_FLOAT : AST_CAST_FLOAT_TO_INTEGER) : codegen_type_is_float(c, re) ? AST_CAST_INTEGER_TO_FLOAT : AST_CAST_INTEGER;

				if (codegen_type_is_bool_vector(c, at)) s.offset = i / 8, s.is_bitfield = 1, s.width = 1, s.bit_offset = i % 8;
				v = cast(e, load(e, &s, ae), ae, re, KIND(c, re) == AST_TYPE_BOOL ? AST_CAST_BOOL : ck);
			}
			if (codegen_type_is_bool_vector(c, x->ty)) d.offset = i / 8, d.is_bitfield = 1, d.width = 1, d.bit_offset = i % 8;
			store(e, &d, re, &v);
			val_free(e, &v);
		}
		tfree(e, ra);
		if (rb >= 0)
			tfree(e, rb);
		return codegen_value_register(rd);
	}
	case CC_BUILTIN_CONSTANT_P:
		return codegen_value_constant(a64_constant_p(e, arg[0]));
	case CC_BUILTIN_FAST_ROOT4: case CC_BUILTIN_FAST_SQRT: {
		int d = KIND(c, x->ty) == AST_TYPE_DOUBLE;
		v = gen(e, arg[0]);
		toreg(e, &v);
		word(e, f_in(d, 16, v.register_number));
		word(e, 0x1e21c000u | (uint32_t)d << 22 | 16u << 5 | 16u);
		if (x->aux == CC_BUILTIN_FAST_ROOT4)
			word(e, 0x1e21c000u | (uint32_t)d << 22 | 16u << 5 | 16u);
		word(e, f_out(d, v.register_number, 16));
		return v;
	}
	case CC_BUILTIN_EXPECT:
	case CC_BUILTIN_EXPECT_PROB:
		/* the hint is still evaluated (argument order is unspecified: first, so that
		 * nothing is live across it) */
		for (uint32_t i = 1; i < x->c; i++)
			if (codegen_expression_has_side_effects(c, arg[i])) {
				v = gen(e, arg[i]);
				val_free(e, &v);
			}
		v = gen(e, arg[0]);
		return v;
	case CC_BUILTIN_ASSUME_ALIGNED:
	case CC_BUILTIN_EXTRACT_RETURN_ADDRESS:
		return gen(e, arg[0]);
	case CC_BUILTIN_ASSUME:
		return codegen_value_none();
	case CC_BUILTIN_VA_END: {	/* nothing to do, but the argument's side effects happen */
		codegen_lvalue l = gen_lval(e, arg[0]);
		codegen_value a = laddr(e, &l);

		val_free(e, &a);
		return codegen_value_none();
	}
	case CC_BUILTIN_VA_START:
		va_start_(e, arg[0]);
		return codegen_value_none();
	case CC_BUILTIN_VA_COPY: {
		codegen_lvalue d = gen_lval(e, arg[0]), s = gen_lval(e, arg[1]);
		codegen_value dv = laddr(e, &d), sv = laddr(e, &s);
		int rd = toreg(e, &dv), rs = toreg(e, &sv);

		copy_mem(e, rd, rs, codegen_type_size(c, c->t_valist));
		val_free(e, &dv);
		val_free(e, &sv);
		return codegen_value_none();
	}
	case CC_BUILTIN_UNREACHABLE:
		e->is_reachable = false;
		return codegen_value_none();
	case CC_BUILTIN_TRAP:
		if (e->is_reachable)
			put(e, I_BRK)->imm = 0x3e8;
		e->is_reachable = false;
		return codegen_value_none();
	case CC_BUILTIN_ARM_NOP: case CC_BUILTIN_ARM_YIELD: case CC_BUILTIN_ARM_WFE: case CC_BUILTIN_ARM_WFI: case CC_BUILTIN_ARM_SEV: case CC_BUILTIN_ARM_SEVL:
	case CC_BUILTIN_ARM_CLREX: case CC_BUILTIN_ARM_ISB: case CC_BUILTIN_ARM_DMB: case CC_BUILTIN_ARM_DSB: case CC_BUILTIN_ARM_TRAP: {
		/* the instruction word: hints (op2 in bits 5-7), barriers (CRm, the operand, in 8-11) */
		static const uint32_t w[] = {
			[CC_BUILTIN_ARM_NOP - CC_BUILTIN_ARM_NOP] = 0xd503201f, [CC_BUILTIN_ARM_YIELD - CC_BUILTIN_ARM_NOP] = 0xd503203f,
			[CC_BUILTIN_ARM_WFE - CC_BUILTIN_ARM_NOP] = 0xd503205f, [CC_BUILTIN_ARM_WFI - CC_BUILTIN_ARM_NOP] = 0xd503207f,
			[CC_BUILTIN_ARM_SEV - CC_BUILTIN_ARM_NOP] = 0xd503209f, [CC_BUILTIN_ARM_SEVL - CC_BUILTIN_ARM_NOP] = 0xd50320bf,
			[CC_BUILTIN_ARM_CLREX - CC_BUILTIN_ARM_NOP] = 0xd5033f5f, [CC_BUILTIN_ARM_ISB - CC_BUILTIN_ARM_NOP] = 0xd50330df,
			[CC_BUILTIN_ARM_DMB - CC_BUILTIN_ARM_NOP] = 0xd50330bf, [CC_BUILTIN_ARM_DSB - CC_BUILTIN_ARM_NOP] = 0xd503309f,
		};
		cc_value cv = { 0 };

		if (x->aux >= CC_BUILTIN_ARM_ISB && (!cc_eval(c, arg[0], &cv) || cv.kind != CC_VALUE_INTEGER))
			codegen_fatal_report(e, x->loc, "argument to '%s' must be a constant integer",
			       x->aux == CC_BUILTIN_ARM_TRAP ? "__builtin_arm_trap" : "__builtin_arm_dmb");
		if (!e->is_reachable)
			return codegen_value_none();
		if (x->aux == CC_BUILTIN_ARM_TRAP)
			put(e, I_BRK)->imm = (int64_t)(cv.i & 0xffff);
		else
			put(e, I_WORD)->imm = w[x->aux - CC_BUILTIN_ARM_NOP] | (x->aux >= CC_BUILTIN_ARM_ISB ? (uint32_t)(cv.i & 15) << 8 : 0);
		return codegen_value_none();
	}
	case CC_BUILTIN_ROTL8: case CC_BUILTIN_ROTL16: case CC_BUILTIN_ROTL32: case CC_BUILTIN_ROTL64:
	case CC_BUILTIN_ROTR8: case CC_BUILTIN_ROTR16: case CC_BUILTIN_ROTR32: case CC_BUILTIN_ROTR64: {
		/* LSLV/LSRV take the count mod the register width: 32/64 bits as
		   (x << n) | (x >> -n); 8/16 bits with n & (w-1), w - n and a mask */
		int k = (x->aux - CC_BUILTIN_ROTL8) & 3, w = 8 << k, ax = w == 64, left = x->aux < CC_BUILTIN_ROTR8;
		codegen_value a = gen(e, arg[0]), b = gen(e, arg[1]);
		int ra = toreg(e, &a), rb = toreg(e, &b), t = treg(e);

		if (w < 32) {
			opi(e, I_ANDI, 0, ra, ra, (1 << w) - 1);
			opi(e, I_ANDI, 0, rb, rb, w - 1);
		}
		op3(e, I_SUB, ax, t, ZR, rb);		/* -n */
		if (w < 32)
			opi(e, I_ADDI, 0, t, t, w);	/* w - n */
		op3(e, left ? I_LSRV : I_LSLV, ax, t, ra, t);
		op3(e, left ? I_LSLV : I_LSRV, ax, ra, ra, rb);
		op3(e, I_ORR, ax, ra, ra, t);
		if (w < 32)
			opi(e, I_ANDI, 0, ra, ra, (1 << w) - 1);
		tfree(e, t);
		val_free(e, &b);
		return a;
	}
	case CC_BUILTIN_DEBUGTRAP:
		if (e->is_reachable)
			put(e, I_BRK)->imm = 0xf000;
		return codegen_value_none();
	case CC_BUILTIN_RETURN_ADDRESS: case CC_BUILTIN_FRAME_ADDRESS: {
		codegen_value depth = gen(e, arg[0]);
		int r = treg(e);
		uint32_t level;

		if (depth.kind != CODEGEN_VALUE_CONSTANT)
			codegen_fatal_report(e, x->loc, "a return or frame address requires a constant level");
		level = (uint32_t)depth.integer;
		mov(e, 1, r, R_FP);
		if (level <= 4) {
			for (uint32_t k = 0; k < level; k++)
				mem(e, I_LD, 3, 0, r, r, 0);
		} else {
			int count = treg(e);
			uint32_t loop = newlabel(e);

			movi(e, 0, count, level);
			deflabel_loop(e, loop, true);
			mem(e, I_LD, 3, 0, r, r, 0);
			opi(e, I_SUBI, 0, count, count, 1);
			cbz(e, true, 0, count, loop);
			tfree(e, count);
		}
		if (x->aux == CC_BUILTIN_RETURN_ADDRESS)
			mem(e, I_LD, 3, 0, r, r, 8);
		return codegen_value_register(r);
	}
	case CC_BUILTIN_OBJECT_SIZE:
	case CC_BUILTIN_DYNAMIC_OBJECT_SIZE: {
		cc_value cv;
		i128 size;
		if (e->function_instance && e->function_instance->is_inlined && codegen_inline_object_size(e, e->function_instance->function, e->function_instance->call, arg[0], arg[1], &size))
			return codegen_value_constant(size);
		if (cc_eval(c, n, &cv) && cv.kind == CC_VALUE_INTEGER)
			return codegen_value_constant(cv.i);
		return codegen_value_constant((i128)(uint64_t)-1);
	}
	case CC_BUILTIN_IS_CONSTANT_EVALUATED:
		return codegen_value_constant(0);
	case CC_BUILTIN_ADD_OVERFLOW: case CC_BUILTIN_SUB_OVERFLOW: case CC_BUILTIN_MUL_OVERFLOW:
		return gen_overflow(e, x, arg);
	case CC_BUILTIN_ADDC: case CC_BUILTIN_SUBC:
		return gen_carry(e, x, arg);
	case CC_BUILTIN_PREFETCH: {
		codegen_value p = gen(e, arg[0]);
		int r = toreg(e, &p);

		if (e->is_reachable)
			put(e, I_PRFM)->rn = (uint8_t)r;
		val_free(e, &p);
		return codegen_value_none();
	}
	case CC_BUILTIN_ALLOCA:
	case CC_BUILTIN_ALLOCA_UNINITIALIZED: {
		codegen_value s = gen(e, arg[0]);
		int r = toreg(e, &s);
		ins *i;

		e->dynamic_frame = true;
		opi(e, I_ADDI, 1, r, r, 15);
		opi(e, I_ANDI, 1, r, r, -16);
		op3(e, I_SUB, 1, SP, SP, r);
		if (e->is_reachable) {
			i = put(e, I_ADDI);
			i->sz = 1, i->rd = (uint8_t)r, i->rn = SP, i->imm = 0, i->cond = 0xee;	/* + outgoing area */
		}
		/* Older VLA scopes must not reclaim this function-lifetime allocation. */
		for (int k = e->function_instance && e->function_instance->is_inlined ? e->function_instance->cleanup_base : 0; k < e->cleanup_count; ++k) {
			uint32_t sym = e->cleanups[k];
			const codegen_binding *b = codegen_symbol_binding_get(e, sym);

			if ((T(c, c->syms[sym].ty)->flags & AST_TYPE_FLAG_VLA) && b->kind == CODEGEN_BINDING_INDIRECT) {
				int sp = treg(e);
				mov(e, 1, sp, SP);
				mem(e, I_ST, 3, 0, sp, FRAME, b->slot + 8);
				tfree(e, sp);
			}
		}
		return s;
	}
	case CC_BUILTIN_ATOMIC_LOAD_N: case CC_BUILTIN_ATOMIC_STORE_N: case CC_BUILTIN_ATOMIC_EXCHANGE_N: case CC_BUILTIN_ATOMIC_CMPXCHG_N:
	case CC_BUILTIN_ATOMIC_LOAD: case CC_BUILTIN_ATOMIC_STORE: case CC_BUILTIN_ATOMIC_EXCHANGE: case CC_BUILTIN_ATOMIC_CMPXCHG:
	case CC_BUILTIN_ATOMIC_FETCH_ADD: case CC_BUILTIN_ATOMIC_FETCH_SUB: case CC_BUILTIN_ATOMIC_FETCH_AND: case CC_BUILTIN_ATOMIC_FETCH_OR:
	case CC_BUILTIN_ATOMIC_FETCH_XOR: case CC_BUILTIN_ATOMIC_ADD_FETCH: case CC_BUILTIN_ATOMIC_SUB_FETCH: case CC_BUILTIN_ATOMIC_AND_FETCH:
	case CC_BUILTIN_ATOMIC_OR_FETCH: case CC_BUILTIN_ATOMIC_XOR_FETCH: case CC_BUILTIN_ATOMIC_FETCH_NAND: case CC_BUILTIN_ATOMIC_FETCH_MAX:
	case CC_BUILTIN_ATOMIC_FETCH_MIN: case CC_BUILTIN_ATOMIC_NAND_FETCH: case CC_BUILTIN_ATOMIC_MAX_FETCH: case CC_BUILTIN_ATOMIC_MIN_FETCH: case CC_BUILTIN_SYNC_VAL_CAS: case CC_BUILTIN_SYNC_BOOL_CAS:
		return gen_atomic(e, x, arg);
	case CC_BUILTIN_ATOMIC_THREAD_FENCE: case CC_BUILTIN_ATOMIC_SIGNAL_FENCE: {
		codegen_value m = gen(e, arg[0]);

		val_free(e, &m);
		if (e->is_reachable && x->aux == CC_BUILTIN_ATOMIC_THREAD_FENCE)
			put(e, I_WORD)->imm = 0xd5033bbf;	/* dmb ish */
		return codegen_value_none();
	}
	case CC_BUILTIN_ATOMIC_LOCK_FREE: {
		for (uint32_t i = 0; i < x->c; i++) {	/* (size, p); __c11_atomic_is_lock_free: (size) */
			codegen_value a = gen(e, arg[i]);

			val_free(e, &a);
		}
		return codegen_value_constant(1);
	}
	case CC_BUILTIN_ARM_RSR: {
		uint32_t sn = codegen_expression_casts_strip(c, arg[0]);
		int r = treg(e);

		if (N(sn)->op != AST_NODE_STRING)
			codegen_fatal_report(e, x->loc, "__builtin_arm_rsr64 needs a string literal");
		if (e->is_reachable) {
			ins *i = put(e, I_MRS);

			i->rd = (uint8_t)r;
			i->ref = N(sn)->a;
		}
		return codegen_value_register(r);
	}
	case CC_BUILTIN_FLT_ROUNDS: {	/* FLT_ROUNDS from FPCR.RMode, as clang: mrs; add #1<<22; ubfx #22, #2 */
		int r = treg(e);

		if (e->is_reachable) {
			ins *i = put(e, I_WORD);

			i->imm = 0xd53b4400 | (unsigned)r;	/* mrs xR, FPCR */
		}
		opi(e, I_ADDI, 0, r, r, 1 << 22);
		opbf(e, I_UBFM, 0, r, r, 22, 23);
		return codegen_value_register(r);
	}
	case CC_BUILTIN_CLZ: case CC_BUILTIN_CLZL: case CC_BUILTIN_CLZLL: case CC_BUILTIN_CTZ: case CC_BUILTIN_CTZL: case CC_BUILTIN_CTZLL:
	case CC_BUILTIN_FFS: case CC_BUILTIN_FFSL: case CC_BUILTIN_FFSLL: case CC_BUILTIN_POPCOUNT: case CC_BUILTIN_POPCOUNTL: case CC_BUILTIN_POPCOUNTLL:
	case CC_BUILTIN_PARITY: case CC_BUILTIN_PARITYL: case CC_BUILTIN_PARITYLL: case CC_BUILTIN_BSWAP16: case CC_BUILTIN_BSWAP32: case CC_BUILTIN_BSWAP64:
	case CC_BUILTIN_CLRSB: case CC_BUILTIN_CLRSBL: case CC_BUILTIN_CLRSBLL: case CC_BUILTIN_BITREVERSE8: case CC_BUILTIN_BITREVERSE16:
	case CC_BUILTIN_BITREVERSE32: case CC_BUILTIN_BITREVERSE64: {
		ast_qualified_type at = N(arg[0])->ty;
		int ax = codegen_type_size(c, at) == 8, r, t;

		v = gen(e, arg[0]);
		if (v.kind == CODEGEN_VALUE_CONSTANT) {
			u128 u = (u128)codegen_integer_truncate(c, v.integer, at) & (ax ? ~(u128)0 >> 64 : 0xffffffffu);
			unsigned bits = ax ? 64 : 32;
			i128 res = 0;

			if (codegen_type_size(c, at) == 2)
				u &= 0xffff;
			switch (x->aux) {
			case CC_BUILTIN_CLZ: case CC_BUILTIN_CLZL: case CC_BUILTIN_CLZLL:
				if (!u)
					goto runtime;
				res = __builtin_clzll((uint64_t)u) - (64 - bits);
				break;
			case CC_BUILTIN_CTZ: case CC_BUILTIN_CTZL: case CC_BUILTIN_CTZLL:
				if (!u)
					goto runtime;
				res = __builtin_ctzll((uint64_t)u);
				break;
			case CC_BUILTIN_FFS: case CC_BUILTIN_FFSL: case CC_BUILTIN_FFSLL:
				res = u ? __builtin_ctzll((uint64_t)u) + 1 : 0;
				break;
			case CC_BUILTIN_POPCOUNT: case CC_BUILTIN_POPCOUNTL: case CC_BUILTIN_POPCOUNTLL:
				res = __builtin_popcountll((uint64_t)u);
				break;
			case CC_BUILTIN_PARITY: case CC_BUILTIN_PARITYL: case CC_BUILTIN_PARITYLL:
				res = __builtin_popcountll((uint64_t)u) & 1;
				break;
			case CC_BUILTIN_BSWAP16: res = __builtin_bswap16((uint16_t)u); break;
			case CC_BUILTIN_BSWAP32: res = __builtin_bswap32((uint32_t)u); break;
			case CC_BUILTIN_BSWAP64: res = (i128)__builtin_bswap64((uint64_t)u); break;
			default:
				goto runtime;
			}
			return codegen_value_constant(codegen_integer_truncate(c, res, x->ty));
		}
runtime:
		r = toreg(e, &v);
		switch (x->aux) {
		case CC_BUILTIN_CLZ: case CC_BUILTIN_CLZL: case CC_BUILTIN_CLZLL:
			op3(e, I_CLZ, ax, r, r, 0);
			break;
		case CC_BUILTIN_CTZ: case CC_BUILTIN_CTZL: case CC_BUILTIN_CTZLL:
			op3(e, I_RBIT, ax, r, r, 0);
			op3(e, I_CLZ, ax, r, r, 0);
			break;
		case CC_BUILTIN_FFS: case CC_BUILTIN_FFSL: case CC_BUILTIN_FFSLL:
			t = treg(e);
			op3(e, I_RBIT, ax, t, r, 0);
			op3(e, I_CLZ, ax, t, t, 0);
			op3(e, I_SUBS, ax, ZR, r, ZR);
			op3s(e, I_CSINC, 0, r, ZR, t, C_EQ, 0);
			tfree(e, t);
			break;
		case CC_BUILTIN_POPCOUNT: case CC_BUILTIN_POPCOUNTL: case CC_BUILTIN_POPCOUNTLL:
			return popcount(e, v, ax);
		case CC_BUILTIN_PARITY: case CC_BUILTIN_PARITYL: case CC_BUILTIN_PARITYLL:
			v = popcount(e, v, ax);
			opi(e, I_ANDI, 0, v.register_number, v.register_number, 1);
			return v;
		case CC_BUILTIN_BSWAP16:
			op3(e, I_REV16, 0, r, r, 0);
			break;
		case CC_BUILTIN_BSWAP32:
			op3(e, I_REV, 0, r, r, 0);
			break;
		case CC_BUILTIN_BSWAP64:
			op3(e, I_REV, 1, r, r, 0);
			break;
		case CC_BUILTIN_CLRSB: case CC_BUILTIN_CLRSBL: case CC_BUILTIN_CLRSBLL:
			op3(e, I_CLS, ax, r, r, 0);
			break;
		case CC_BUILTIN_BITREVERSE8:
			op3(e, I_RBIT, 0, r, r, 0);
			shift_imm(e, I_LSRV, 0, r, r, 24);
			break;
		case CC_BUILTIN_BITREVERSE16:
			op3(e, I_RBIT, 0, r, r, 0);
			shift_imm(e, I_LSRV, 0, r, r, 16);
			break;
		case CC_BUILTIN_BITREVERSE32:
			op3(e, I_RBIT, 0, r, r, 0);
			break;
		case CC_BUILTIN_BITREVERSE64:
			op3(e, I_RBIT, 1, r, r, 0);
			break;
		}
		return v;
	}
	case CC_BUILTIN_ISGREATER: case CC_BUILTIN_ISGREATEREQUAL: case CC_BUILTIN_ISLESS: case CC_BUILTIN_ISLESSEQUAL:
	case CC_BUILTIN_ISLESSGREATER: case CC_BUILTIN_ISUNORDERED: {
		ast_qualified_type at = N(arg[0])->ty, bt = N(arg[1])->ty;
		ast_qualified_type ct = codegen_type_is_long_double(c, at) ? at : codegen_type_is_long_double(c, bt) ? bt :
		         (codegen_type_is_float(c, at) && codegen_type_size(c, at) == 8) ||
		         (codegen_type_is_float(c, bt) && codegen_type_size(c, bt) == 8) ? codegen_type_primitive(AST_TYPE_DOUBLE) : codegen_type_primitive(AST_TYPE_FLOAT);
		codegen_value a = cast(e, gen(e, arg[0]), at, ct, AST_CAST_INTEGER);
		codegen_value b = cast(e, gen(e, arg[1]), bt, ct, AST_CAST_INTEGER);
		int op = x->aux == CC_BUILTIN_ISGREATER ? LEX_TOKEN_GREATER :
		         x->aux == CC_BUILTIN_ISGREATEREQUAL ? LEX_TOKEN_GREATER_EQUAL :
		         x->aux == CC_BUILTIN_ISLESS ? LEX_TOKEN_LESS :
		         x->aux == CC_BUILTIN_ISLESSEQUAL ? LEX_TOKEN_LESS_EQUAL : LEX_TOKEN_EXCLAMATION_EQUAL;

		if (!codegen_type_is_long_double(c, ct)) {
			int cond = f_cmp(e, op, fpk(e, ct), &a, &b);
			val_free(e, &b);
			if (x->aux == CC_BUILTIN_ISUNORDERED)
				cond = C_VS;
			cset(e, a.register_number, cond);
			if (x->aux == CC_BUILTIN_ISLESSGREATER) {
				int ordered = treg(e);
				cset(e, ordered, C_VC);
				op3(e, I_AND, 0, a.register_number, a.register_number, ordered);
				tfree(e, ordered);
			}
			return codegen_value_register(a.register_number);
		}
		if (x->aux != CC_BUILTIN_ISUNORDERED && x->aux != CC_BUILTIN_ISLESSGREATER)
			return f_compare(e, op, ct, a, b);

		/* Binary128 NaNs: exponent all ones and a nonzero fraction.
		 * Inspect bits so quiet NaNs do not raise FE_INVALID. Keep the
		 * original operands for the ordered inequality when needed. */
		codegen_lvalue saved[2];
		codegen_value operands[2] = { a, b };
		int nanregs[2];
		for (int i = 0; i < 2; i++) {
			if (x->aux == CC_BUILTIN_ISLESSGREATER) {
				saved[i] = (codegen_lvalue){ .kind = CODEGEN_LVALUE_FRAME, .offset = slot(e, 16, 16), .align = 16 };
				store(e, &saved[i], ct, &operands[i]);
			}
			toreg2(e, &operands[i], false);
			int lo = operands[i].register_number, hi = operands[i].register_high;
			opi(e, I_SUBSI, 1, ZR, lo, 0);
			cset(e, lo, C_NE);
			shift_imm(e, LEX_TOKEN_LESS_LESS, 1, hi, hi, 1);
			op3(e, I_ORR, 1, hi, hi, lo);
			tfree(e, lo);
			opi(e, I_SUBSI, 1, ZR, hi, (int64_t)0xfffe000000000000ULL);
			cset(e, hi, C_HI);
			nanregs[i] = hi;
		}
		op3(e, I_ORR, 0, nanregs[0], nanregs[0], nanregs[1]);
		tfree(e, nanregs[1]);
		if (x->aux == CC_BUILTIN_ISUNORDERED)
			return codegen_value_register(nanregs[0]);
		opi(e, I_EORI, 0, nanregs[0], nanregs[0], 1);
		a = load(e, &saved[0], ct);
		b = load(e, &saved[1], ct);
		codegen_value ne = f_compare(e, LEX_TOKEN_EXCLAMATION_EQUAL, ct, a, b);
		toreg(e, &ne);
		op3(e, I_AND, 0, ne.register_number, ne.register_number, nanregs[0]);
		tfree(e, nanregs[0]);
		return ne;
	}
	case CC_BUILTIN_ISINF_SIGN: {
		/* Inspect the representation once: return +1/-1 for infinities,
		   zero for every other value, without quieting signaling NaNs. */
		ast_qualified_type at = N(arg[0])->ty;
		codegen_value a = gen(e, arg[0]);
		bool ld = codegen_type_is_long_double(c, at), half = KIND(c, at) == AST_TYPE_FLOAT16 || KIND(c, at) == AST_TYPE_BFLOAT16;
		bool bf = KIND(c, at) == AST_TYPE_BFLOAT16;
		int d = ld || fpk(e, at), r, sign = treg(e);
		uint64_t inf;
		if (ld) {
			toreg2(e, &a, false);
			r = a.register_high;
			shift_imm(e, I_ASRV, 1, sign, r, 63);
			opi(e, I_SUBSI, 1, ZR, a.register_number, 0);
			cset(e, a.register_number, C_NE);
			shift_imm(e, LEX_TOKEN_LESS_LESS, 1, r, r, 1);
			op3(e, I_ORR, 1, r, r, a.register_number);
			tfree(e, a.register_number);
			inf = UINT64_C(0xfffe000000000000);
		} else {
			r = toreg(e, &a);
			if (half) {
				shift_imm(e, LEX_TOKEN_LESS_LESS, 0, sign, r, 16);
				shift_imm(e, I_ASRV, 0, sign, sign, 31);
				opi(e, I_ANDI, 0, r, r, 0x7fff);
				inf = bf ? 0x7f80 : 0x7c00;
			} else {
				shift_imm(e, I_ASRV, d, sign, r, d ? 63 : 31);
				shift_imm(e, LEX_TOKEN_LESS_LESS, d, r, r, 1);
				inf = d ? UINT64_C(0xffe0000000000000) : UINT64_C(0xff000000);
			}
		}
		opi(e, I_ORRI, 0, sign, sign, 1);
		opi(e, I_SUBSI, d, ZR, r, (int64_t)inf);
		cset(e, r, C_EQ);
		op3(e, I_SUB, 0, r, ZR, r);
		op3(e, I_AND, 0, r, r, sign);
		tfree(e, sign);
		return codegen_value_register(r);
	}
	case CC_BUILTIN_ISSIGNALING: {
		ast_qualified_type at = N(arg[0])->ty;
		codegen_value a = gen(e, arg[0]);
		int r, d;
		uint64_t inf, quiet;
		if (codegen_type_is_long_double(c, at)) {
			/* Fold low payload bits into a sticky bit after discarding sign. */
			toreg2(e, &a, false);
			r = a.register_high, d = 1;
			opi(e, I_SUBSI, 1, ZR, a.register_number, 0);
			cset(e, a.register_number, C_NE);
			shift_imm(e, LEX_TOKEN_LESS_LESS, 1, r, r, 1);
			op3(e, I_ORR, 1, r, r, a.register_number);
			tfree(e, a.register_number);
			inf = UINT64_C(0xfffe000000000000), quiet = UINT64_C(1) << 48;
		} else {
			bool half = KIND(c, at) == AST_TYPE_FLOAT16 || KIND(c, at) == AST_TYPE_BFLOAT16;
			bool bf = KIND(c, at) == AST_TYPE_BFLOAT16;
			d = fpk(e, at);
			r = toreg(e, &a);
			opi(e, I_ANDI, d, r, r, half ? 0x7fff : d ? INT64_MAX : 0x7fffffff);
			inf = bf ? UINT64_C(0x7f80) : half ? UINT64_C(0x7c00) : d ? UINT64_C(0x7ff0000000000000) : UINT64_C(0x7f800000);
			quiet = UINT64_C(1) << (bf ? 6 : half ? 9 : d ? 51 : 22);
		}
		opi(e, I_SUBI, d, r, r, (int64_t)(inf + 1));
		opi(e, I_SUBSI, d, ZR, r, (int64_t)(quiet - 1));
		cset(e, r, C_LO);
		return codegen_value_register(r);
	}
	case CC_BUILTIN_ISNAN: case CC_BUILTIN_ISINF: case CC_BUILTIN_ISFINITE: case CC_BUILTIN_SIGNBIT: case CC_BUILTIN_ISNORMAL: {
		cc_value cv;

		if (cc_eval(c, n, &cv) && cv.kind == CC_VALUE_INTEGER)
			return codegen_value_constant(cv.i);
		/* on the bit pattern: sign out (<< 1), then the exponent against all ones */
		ast_qualified_type at = N(arg[0])->ty;
		bool ld = codegen_type_is_long_double(c, at);
		int d = ld || fpk(e, at), bits = d ? 64 : 32, mant = ld ? 48 : d ? 52 : 23, r;

		v = gen(e, arg[0]);
		if (ld) {
			/* the high word, with a nonzero low word as a sticky bit 0 (below the shift) */
			toreg2(e, &v, false);
			r = v.register_high;
			if (x->aux != CC_BUILTIN_SIGNBIT) {
				opi(e, I_SUBSI, 1, ZR, v.register_number, 0);
				cset(e, v.register_number, C_NE);
				shift_imm(e, LEX_TOKEN_LESS_LESS, 1, r, r, 1);
				op3(e, I_ORR, 1, r, r, v.register_number);
				tfree(e, v.register_number);
				if (x->aux == CC_BUILTIN_ISNORMAL) {
					shift_imm(e, I_LSRV, 1, r, r, mant + 1);
					opi(e, I_SUBI, 1, r, r, 1);
					opi(e, I_SUBSI, 1, ZR, r, 0x7fff - 1);
					cset(e, r, C_LO);
					return codegen_value_register(r);
				}
				opi(e, I_SUBSI, 1, ZR, r, (int64_t)0xfffe000000000000ULL);
				cset(e, r, x->aux == CC_BUILTIN_ISNAN ? C_HI : x->aux == CC_BUILTIN_ISINF ? C_EQ : C_LO);
				return codegen_value_register(r);
			}
			tfree(e, v.register_number);
		} else {
			r = toreg(e, &v);
		}
		if (KIND(c, at) == AST_TYPE_FLOAT16 || KIND(c, at) == AST_TYPE_BFLOAT16) {
			bool bf = KIND(c, at) == AST_TYPE_BFLOAT16;
			if (x->aux == CC_BUILTIN_SIGNBIT) {
				shift_imm(e, I_LSRV, 0, r, r, 15);
				opi(e, I_ANDI, 0, r, r, 1);
				return codegen_value_register(r);
			}
			opi(e, I_ANDI, 0, r, r, 0x7fff);
			if (x->aux == CC_BUILTIN_ISNORMAL) {
				shift_imm(e, I_LSRV, 0, r, r, bf ? 7 : 10);
				opi(e, I_SUBI, 0, r, r, 1);
				opi(e, I_SUBSI, 0, ZR, r, bf ? 254 : 30);
				cset(e, r, C_LO);
			} else {
				opi(e, I_SUBSI, 0, ZR, r, bf ? 0x7f80 : 0x7c00);
				cset(e, r, x->aux == CC_BUILTIN_ISNAN ? C_HI : x->aux == CC_BUILTIN_ISINF ? C_EQ : C_LO);
			}
			return codegen_value_register(r);
		}
		if (x->aux == CC_BUILTIN_SIGNBIT) {
			shift_imm(e, I_LSRV, d, r, r, bits - 1);
			return codegen_value_register(r);
		}
		shift_imm(e, LEX_TOKEN_LESS_LESS, d, r, r, 1);
		if (x->aux == CC_BUILTIN_ISNORMAL) {
			shift_imm(e, I_LSRV, d, r, r, mant + 1);
			opi(e, I_SUBI, d, r, r, 1);
			opi(e, I_SUBSI, d, ZR, r, (bits == 64 ? 0x7ff : 0xff) - 1);
			cset(e, r, C_LO);
			return codegen_value_register(r);
		}
		opi(e, I_SUBSI, d, ZR, r, d ? (int64_t)0xffe0000000000000ULL : 0xff000000LL);
		cset(e, r, x->aux == CC_BUILTIN_ISNAN ? C_HI : x->aux == CC_BUILTIN_ISINF ? C_EQ : C_LO);
		return codegen_value_register(r);
	}
	default: {
		cc_value cv;

		if (cc_eval(c, n, &cv) && cv.kind == CC_VALUE_INTEGER)
			return codegen_value_constant(cv.i);
		if (cc_eval(c, n, &cv) && cv.kind == CC_VALUE_FLOAT && codegen_type_is_float(c, x->ty))
			return f_const_cv(e, x->ty, &cv);
		codegen_fatal_report(e, x->loc, "unsupported builtin (%d)", x->aux);
	}
	}
}
