/* The walker's lowering interface for values: conversions, registers, integer and floating point operations. */
#define WK_PREFIX virbuild_
#define LO_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- conversions ---------------- */

VPRIV uint32_t virbuild_value_convert_in_block(ecx *e, uint32_t b, uint32_t v, uint32_t to)
{
	virbuild_context *V = e->vw;
	uint32_t from = V->insns[v].insn.type;

	if (from == to)
		return v;
	if (V->insns[v].insn.op == VIR_OP_ICONST && to != VIR_TYPE_F32 && to != VIR_TYPE_F64) {
		uint64_t x = V->insns[v].insn.b | (uint64_t)V->insns[v].insn.c << 32;

		return virbuild_constant_integer(e, to, to == VIR_TYPE_I32 ? (int64_t)(uint32_t)x : (int64_t)x);
	}
#define I(op, t, fl, a) virbuild_block_insn_append(e, b, op, t, fl, a, 0, 0)
	switch (from * 8 + to) {
	case VIR_TYPE_I32 * 8 + VIR_TYPE_I64: return I(VIR_OP_EXT, VIR_TYPE_I64, 2, v);
	case VIR_TYPE_I64 * 8 + VIR_TYPE_I32: return I(VIR_OP_TRUNC, VIR_TYPE_I32, 0, v);
	case VIR_TYPE_PTR * 8 + VIR_TYPE_I64: return I(VIR_OP_P2I, VIR_TYPE_I64, 0, v);
	case VIR_TYPE_I64 * 8 + VIR_TYPE_PTR: return I(VIR_OP_I2P, VIR_TYPE_PTR, 0, v);
	case VIR_TYPE_I32 * 8 + VIR_TYPE_PTR: return I(VIR_OP_I2P, VIR_TYPE_PTR, 0, I(VIR_OP_EXT, VIR_TYPE_I64, 2, v));
	case VIR_TYPE_PTR * 8 + VIR_TYPE_I32: return I(VIR_OP_TRUNC, VIR_TYPE_I32, 0, I(VIR_OP_P2I, VIR_TYPE_I64, 0, v));
	case VIR_TYPE_I32 * 8 + VIR_TYPE_F32: case VIR_TYPE_I64 * 8 + VIR_TYPE_F64:
	case VIR_TYPE_F32 * 8 + VIR_TYPE_I32: case VIR_TYPE_F64 * 8 + VIR_TYPE_I64: return I(VIR_OP_BITCAST, to, 0, v);
	case VIR_TYPE_I64 * 8 + VIR_TYPE_F32: return I(VIR_OP_BITCAST, VIR_TYPE_F32, 0, I(VIR_OP_TRUNC, VIR_TYPE_I32, 0, v));
	case VIR_TYPE_F32 * 8 + VIR_TYPE_I64: return I(VIR_OP_EXT, VIR_TYPE_I64, 2, I(VIR_OP_BITCAST, VIR_TYPE_I32, 0, v));
	default: break;
	}
#undef I
	virbuild_fail(e, VIRBUILD_FAILURE(value_type_mismatch));
}

VPRIV uint32_t virbuild_value_convert(ecx *e, uint32_t v, uint32_t to) { return virbuild_value_convert_in_block(e, e->vw->current_block, v, to); }

/* name r's value as type ty */
VPRIV uint32_t virbuild_register_read(ecx *e, int r, uint32_t ty)
{
	if (r < 0 || r >= VIRBUILD_REGISTER_NONE)
		virbuild_fail(e, VIRBUILD_FAILURE(no_such_register));
	return virbuild_value_convert(e, virbuild_variable_read(e, (uint32_t)r, e->vw->current_block, ty), ty);
}

VPRIV uint32_t virbuild_register_read_raw(ecx *e, int r)	/* name r's value as it is */
{
	virbuild_context *V = e->vw;
	uint32_t *d = virbuild_variable_slot(e, V->current_block, (uint32_t)r, false);

	if (d)
		return *d;
	return virbuild_variable_read(e, (uint32_t)r, V->current_block, r >= VIRBUILD_FLOAT_REGISTER_FIRST ? VIR_TYPE_F64 : VIR_TYPE_I64);
}

VPRIV void virbuild_register_write(ecx *e, int r, uint32_t v)
{
	if (r < 0 || r >= VIRBUILD_REGISTER_NONE)
		virbuild_fail(e, VIRBUILD_FAILURE(no_such_register));
	if (r >= VIRBUILD_FLOAT_REGISTER_FIRST)
		e->vw->float_products[r - VIRBUILD_FLOAT_REGISTER_FIRST] = 0;
	virbuild_variable_write(e, (uint32_t)r, e->vw->current_block, v);
}

VPRIV uint32_t virbuild_integer_type(int size) { return size == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32; }

/* a walker value as a VIR value of type ty */
VPRIV uint32_t virbuild_walker_value_get(ecx *e, const val *v, uint32_t ty)
{
	switch (v->k) {
	case V_CONST:
		if (ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64)
			virbuild_fail(e, VIRBUILD_FAILURE(integer_constant_as_float));
		return virbuild_constant_integer(e, ty, (int64_t)v->i);
	case V_ADDR: {
		uint32_t a = virbuild_constant_make(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, v->ref, (uint32_t)(uint64_t)v->i, (uint32_t)((uint64_t)v->i >> 32));

		note_ref(e, v->ref);
		return virbuild_value_convert(e, a, ty);
	}
	case V_REG:
		return virbuild_register_read(e, v->r, ty);
	default:
		virbuild_fail(e, VIRBUILD_FAILURE(unsupported_value_kind));
	}
}

/* the type a value of the walker's has (V_REG: its name's current value's) */
static uint32_t virbuild_walker_value_type(ecx *e, const val *v)
{
	if (v->k == V_ADDR)
		return VIR_TYPE_PTR;
	if (v->k == V_REG)
		return virbuild_value_type(e, virbuild_register_read_raw(e, v->r));
	return VIR_TYPE_I64;
}

/* ---------------- registers ---------------- */

VPRIV int virbuild_treg(ecx *e)
{
	virbuild_context *V = e->vw;

	for (int w = 0; w < 3; w++)
		if (~V->live_names[w]) {
			int k = __builtin_ctzll(~V->live_names[w]);

			V->live_names[w] |= 1ull << k;
			return w * 64 + k;
		}
	virbuild_fail(e, VIRBUILD_FAILURE(out_of_names));
}

VPRIV int virbuild_ftreg(ecx *e)
{
	virbuild_context *V = e->vw;

	for (int r = VIRBUILD_FLOAT_REGISTER_FIRST; r < VIRBUILD_REGISTER_NONE; r++)
		if (!(V->live_float_names >> (r - VIRBUILD_FLOAT_REGISTER_FIRST) & 1)) {
			V->live_float_names |= 1ull << (r - VIRBUILD_FLOAT_REGISTER_FIRST);
			return r;
		}
	virbuild_fail(e, VIRBUILD_FAILURE(out_of_float_names));
}

VPRIV void virbuild_tfree(ecx *e, int r)
{
	virbuild_context *V = e->vw;

	if (r >= 0 && r < VIRBUILD_FLOAT_REGISTER_FIRST)
		V->live_names[r / 64] &= ~(1ull << (r % 64));
	else if (r >= VIRBUILD_FLOAT_REGISTER_FIRST && r < VIRBUILD_REGISTER_NONE)
		V->live_float_names &= ~(1ull << (r - VIRBUILD_FLOAT_REGISTER_FIRST));
}

VPRIV void virbuild_tkeep(ecx *e, int r)
{
	virbuild_context *V = e->vw;

	if (r >= 0 && r < VIRBUILD_FLOAT_REGISTER_FIRST)
		V->live_names[r / 64] |= 1ull << (r % 64);
	else if (r >= VIRBUILD_FLOAT_REGISTER_FIRST && r < VIRBUILD_REGISTER_NONE)
		V->live_float_names |= 1ull << (r - VIRBUILD_FLOAT_REGISTER_FIRST);
}

VPRIV int virbuild_nfree(ecx *e)
{
	virbuild_context *V = e->vw;

	return 192 - __builtin_popcountll(V->live_names[0]) - __builtin_popcountll(V->live_names[1]) - __builtin_popcountll(V->live_names[2]);
}

VPRIV int virbuild_nffree(ecx *e) { return 63 - __builtin_popcountll(e->vw->live_float_names); }

VPRIV void virbuild_mov(ecx *e, int rd, int rs)
{
	if (rd == rs)
		return;
	virbuild_register_write(e, rd, virbuild_register_read_raw(e, rs));
}

VPRIV int virbuild_toreg(ecx *e, val *v, int size)
{
	int r;

	(void)size;
	if (v->k == V_REG)
		return v->r;
	r = virbuild_treg(e);
	virbuild_register_write(e, r, virbuild_walker_value_get(e, v, v->k == V_ADDR ? VIR_TYPE_PTR : VIR_TYPE_I64));
	*v = (val){ .k = V_REG, .r = (uint8_t)r };
	return r;
}

/* a result's name: a's if a is in an integer name, else a new one; b's freed */
VPRIV int virbuild_result_register_pick(ecx *e, const val *a, const val *b)
{
	int r = a->k == V_REG && a->r < VIRBUILD_FLOAT_REGISTER_FIRST ? a->r : virbuild_treg(e);

	if (b && b->k == V_REG && b->r != r)
		virbuild_tfree(e, b->r);
	return r;
}

/* ---------------- integer operations ---------------- */

VPRIV val virbuild_umulhi(ecx *e, val a, val b)
{
	uint32_t x = virbuild_walker_value_get(e, &a, VIR_TYPE_I64), y = virbuild_walker_value_get(e, &b, VIR_TYPE_I64);
	int r = virbuild_result_register_pick(e, &a, &b);

	virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_UMULHI, VIR_TYPE_I64, 0, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val virbuild_smulhi(ecx *e, val a, val b)
{
	uint32_t x = virbuild_walker_value_get(e, &a, VIR_TYPE_I64), y = virbuild_walker_value_get(e, &b, VIR_TYPE_I64);
	int r = virbuild_result_register_pick(e, &a, &b);

	virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_SMULHI, VIR_TYPE_I64, 0, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val virbuild_bin(ecx *e, int op, int size, bool sgn, val a, val b)
{
	uint32_t ty = virbuild_integer_type(size), vop, x, y, at, bt;
	int r;

	at = virbuild_walker_value_type(e, &a), bt = virbuild_walker_value_type(e, &b);
	if (size == 8 && (op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS) && (at == VIR_TYPE_PTR || bt == VIR_TYPE_PTR)) {
		/* pointer arithmetic */
		if (op == LEX_TOKEN_PLUS && at != VIR_TYPE_PTR) {
			val t = a;

			a = b, b = t;
			at = VIR_TYPE_PTR, bt = virbuild_walker_value_type(e, &b);
		}
		if (at == VIR_TYPE_PTR && !(op == LEX_TOKEN_MINUS && bt == VIR_TYPE_PTR)) {
			x = virbuild_walker_value_get(e, &a, VIR_TYPE_PTR);
			y = virbuild_walker_value_get(e, &b, VIR_TYPE_I64);
			if (op == LEX_TOKEN_MINUS)
				y = b.k == V_CONST ? virbuild_constant_integer(e, VIR_TYPE_I64, -(int64_t)b.i) : virbuild_insn_append(e, VIR_OP_NEG, VIR_TYPE_I64, 0, y, 0, 0);
			r = virbuild_result_register_pick(e, &a, &b);
			virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, x, y, 0));
			return (val){ .k = V_REG, .r = (uint8_t)r };
		}
		if (op == LEX_TOKEN_MINUS && at == VIR_TYPE_PTR && bt == VIR_TYPE_PTR) {
			x = virbuild_walker_value_get(e, &a, VIR_TYPE_PTR);
			y = virbuild_walker_value_get(e, &b, VIR_TYPE_PTR);
			r = virbuild_result_register_pick(e, &a, &b);
			virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_PSUB, VIR_TYPE_I64, 0, x, y, 0));
			return (val){ .k = V_REG, .r = (uint8_t)r };
		}
	}
	switch (op) {
	case LEX_TOKEN_PLUS: vop = VIR_OP_ADD; break;
	case LEX_TOKEN_MINUS: vop = VIR_OP_SUB; break;
	case LEX_TOKEN_STAR: vop = VIR_OP_MUL; break;
	case LEX_TOKEN_SLASH: vop = sgn ? VIR_OP_SDIV : VIR_OP_UDIV; break;
	case LEX_TOKEN_PERCENT: vop = sgn ? VIR_OP_SREM : VIR_OP_UREM; break;
	case LEX_TOKEN_AMPERSAND: vop = VIR_OP_AND; break;
	case LEX_TOKEN_PIPE: vop = VIR_OP_OR; break;
	case LEX_TOKEN_CARET: vop = VIR_OP_XOR; break;
	case LEX_TOKEN_LESS_LESS: vop = VIR_OP_SHL; break;
	case LEX_TOKEN_GREATER_GREATER: vop = sgn ? VIR_OP_ASHR : VIR_OP_LSHR; break;
	default: virbuild_fail(e, VIRBUILD_FAILURE(unsupported_operator));
	}
	uint8_t fl=sgn && !e->E->o->wrapv && (vop==VIR_OP_ADD || vop==VIR_OP_SUB || vop==VIR_OP_MUL)?VIR_INSN_FLAG_NSW:0;
	if(vop==VIR_OP_MUL && (fl&VIR_INSN_FLAG_NSW) && (size==4 || size==8) && b.k==V_CONST && b.i>0 &&
	   b.i<=(size==4?INT32_MAX:INT64_MAX) &&
	   !((uint64_t)b.i&((uint64_t)b.i-1))) {
		vop=VIR_OP_SHL;b.i=__builtin_ctzll((uint64_t)b.i);
	}
	if ((vop == VIR_OP_SHL || vop == VIR_OP_ASHR || vop == VIR_OP_LSHR) && b.k == V_CONST)
		b.i &= size * 8 - 1;
	if (a.k == V_CONST && b.k == V_CONST && vop != VIR_OP_SDIV && vop != VIR_OP_UDIV && vop != VIR_OP_SREM && vop != VIR_OP_UREM) {
		/* (the walker folds; what reaches here is rare) */
	}
	x = virbuild_walker_value_get(e, &a, ty);
	y = virbuild_walker_value_get(e, &b, ty);
	r = virbuild_result_register_pick(e, &a, &b);
	/* signed +, -, *: overflow is undefined (not under -fwrapv), which is what makes an index linear */
	virbuild_register_write(e, r, virbuild_insn_append(e, vop, ty, fl, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val virbuild_un(ecx *e, int op, int size, val a)
{
	uint32_t ty = virbuild_integer_type(size), x = virbuild_walker_value_get(e, &a, ty);
	int r = virbuild_result_register_pick(e, &a, NULL);

	virbuild_register_write(e, r, virbuild_insn_append(e, op == LEX_TOKEN_MINUS ? VIR_OP_NEG : VIR_OP_NOT, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV void virbuild_ext(ecx *e, int r, int from, bool sgn)
{
	uint32_t v;

	if (from >= 8)
		return;
	v = virbuild_register_read_raw(e, r);
	if (e->vw->insns[v].insn.op == VIR_OP_ICONST) {	/* a constant: folded */
		uint64_t x = e->vw->insns[v].insn.b | (uint64_t)e->vw->insns[v].insn.c << 32;
		int sh = 64 - 8 * from;

		x = sgn ? (uint64_t)((int64_t)(x << sh) >> sh) : x << sh >> sh;
		virbuild_register_write(e, r, virbuild_constant_integer(e, !sgn && from == 4 ? VIR_TYPE_I32 : VIR_TYPE_I64, (int64_t)x));
		return;
	}
	if (from == 4 && !sgn) {
		virbuild_register_write(e, r, virbuild_value_convert(e, v, VIR_TYPE_I32));
		return;
	}
	if (virbuild_value_type(e, v) == VIR_TYPE_PTR)
		v = virbuild_value_convert(e, v, VIR_TYPE_I64);
	if (virbuild_value_type(e, v) == VIR_TYPE_I32 && from == 4) {	/* sign extension of an i32 */
		virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_EXT, VIR_TYPE_I64, 2 | VIR_INSN_FLAG_MS, v, 0, 0));
		return;
	}
	if (virbuild_value_type(e, v) == VIR_TYPE_F32 || virbuild_value_type(e, v) == VIR_TYPE_F64)
		virbuild_fail(e, VIRBUILD_FAILURE(float_extension));
	/* narrow: the result as wide as the operand (an i32 with a zero upper half, or i64 for a sign) */
	if (sgn)
		v = virbuild_value_convert(e, v, VIR_TYPE_I64);
	virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_EXT, virbuild_value_type(e, v), virbuild_integer_log2((uint64_t)from) | (sgn ? VIR_INSN_FLAG_MS : 0), v, 0, 0));
}

static uint32_t virbuild_integer_condition_code(int op, bool sgn)
{
	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: return 0;
	case LEX_TOKEN_EXCLAMATION_EQUAL: return 1;
	case LEX_TOKEN_LESS: return sgn ? 2 : 6;
	case LEX_TOKEN_LESS_EQUAL: return sgn ? 3 : 7;
	case LEX_TOKEN_GREATER: return sgn ? 4 : 8;
	default: return sgn ? 5 : 9;
	}
}

static uint32_t virbuild_integer_compare(ecx *e, int op, int size, bool sgn, val *a, val *b)
{
	uint32_t ty = virbuild_integer_type(size), x, y;

	if (size == 8 && !sgn && virbuild_walker_value_type(e, a) == VIR_TYPE_PTR && virbuild_walker_value_type(e, b) == VIR_TYPE_PTR)
		ty = VIR_TYPE_PTR;
	x = virbuild_walker_value_get(e, a, ty);
	y = virbuild_walker_value_get(e, b, ty);
	return virbuild_insn_append(e, VIR_OP_ICMP, VIR_TYPE_I32, virbuild_integer_condition_code(op, sgn), x, y, 0);
}

VPRIV val virbuild_cmp(ecx *e, int op, int size, bool sgn, val a, val b)
{
	uint32_t c = virbuild_integer_compare(e, op, size, sgn, &a, &b);
	int r = virbuild_result_register_pick(e, &a, &b);

	virbuild_register_write(e, r, c);
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* to lbl if c (an i32 or i64) is nonzero, else on */
static void virbuild_branch_if(ecx *e, uint32_t c, uint32_t lbl, bool nz)
{
	virbuild_context *V = e->vw;
	uint32_t t = virbuild_label_block(e, lbl), f = virbuild_block_new(e);

	V->blocks[V->current_block].terminator = virbuild_preir_insn_add(e, VIR_OP_BRIF, VIR_TYPE_VOID, 0, c, nz ? t : f, nz ? f : t, V->current_block);
	V->current_block = f;
	/* (the fallthrough's only predecessor is this block: the store table stays valid) */
	V->blocks[f].falls_through = 1;
}

VPRIV void virbuild_cmpbr(ecx *e, int op, int size, bool sgn, val a, val b, uint32_t lbl)
{
	uint32_t c = virbuild_integer_compare(e, op, size, sgn, &a, &b);

	if (a.k == V_REG) virbuild_tfree(e, a.r);
	if (b.k == V_REG) virbuild_tfree(e, b.r);
	virbuild_branch_if(e, c, lbl, true);
}

VPRIV void virbuild_brz(ecx *e, val a, int size, bool nz, uint32_t lbl)
{
	uint32_t t = virbuild_walker_value_type(e, &a), c;

	if (t == VIR_TYPE_PTR)
		c = virbuild_insn_append(e, VIR_OP_ICMP, VIR_TYPE_I32, 1, virbuild_walker_value_get(e, &a, VIR_TYPE_PTR), virbuild_constant_integer(e, VIR_TYPE_PTR, 0), 0);
	else
		c = virbuild_walker_value_get(e, &a, virbuild_integer_type(size));
	if (a.k == V_REG) virbuild_tfree(e, a.r);
	virbuild_branch_if(e, c, lbl, nz);
}

VPRIV void virbuild_switch_bitset(ecx *e, int r, int size, const em_switch_bitset_recipe *recipe, uint32_t target, uint32_t dflt)
{
	(void)r, (void)size, (void)recipe, (void)target, (void)dflt;
	virbuild_fail(e, VIRBUILD_FAILURE(switch_bit_set));
}

static int virbuild_switch_case_compare(const void *a, const void *b)
{
	const uint64_t *x = a, *y = b;

	return x[0] < y[0] ? -1 : x[0] > y[0];
}

VPRIV void virbuild_switch_table(ecx *e, int r, int size, const em_switch_case *cases, uint32_t n, uint32_t dflt)
{
	virbuild_context *V = e->vw;
	uint32_t ty = virbuild_integer_type(size), x = virbuild_register_read(e, r, ty), st = size == 8 ? 5 : 3, m = 0;
	uint64_t mask = size == 8 ? ~0ull : 0xffffffffull, *c3 = malloc((size_t)(2 * n + 1) * 3 * sizeof *c3);
	uint32_t o;

	if (!c3)
		virbuild_fail(e, VIRBUILD_FAILURE(out_of_memory));
	/* VIR's cases are unsigned and sorted: a range across the sign splits in two */
	for (uint32_t k = 0; k < n; k++) {
		uint64_t lo = (uint64_t)cases[k].lo & mask, hi = (uint64_t)cases[k].hi & mask, t = virbuild_label_block(e, cases[k].target);

		if (lo > hi) {
			c3[3 * m] = lo, c3[3 * m + 1] = mask, c3[3 * m + 2] = t, m++;
			lo = 0;
		}
		c3[3 * m] = lo, c3[3 * m + 1] = hi, c3[3 * m + 2] = t, m++;
	}
	qsort(c3, m, 3 * sizeof *c3, virbuild_switch_case_compare);
	o = virbuild_extra_append(e, &m, 1);
	VIRBUILD_ARRAY_GROW(V->extra, V->extra_count, V->extra_capacity, m * st);
	for (uint32_t k = 0; k < m; k++) {
		uint64_t lo = c3[3 * k], hi = c3[3 * k + 1];
		uint32_t *w = &V->extra[V->extra_count];

		if (size == 8) {
			w[0] = (uint32_t)lo, w[1] = (uint32_t)(lo >> 32), w[2] = (uint32_t)hi, w[3] = (uint32_t)(hi >> 32);
		} else {
			w[0] = (uint32_t)lo, w[1] = (uint32_t)hi;
		}
		w[st - 1] = (uint32_t)c3[3 * k + 2];
		V->extra_count += st;
	}
	free(c3);
	virbuild_block_terminate(e, VIR_OP_SWITCH, x, virbuild_label_block(e, dflt), o, 0);
	e->reach = false;
}

/* __int128 and _BitInt: not in 1.0 (SPEC 19) */
VPRIV bool virbuild_ldmem(void) { return true; }
VPRIV bool virbuild_i128mem(void) { return true; }
#define VIRBUILD_STUB_UNSUPPORTED(name, ...) VPRIV val virbuild_##name(ecx *e, __VA_ARGS__) { virbuild_fail(e, VIRBUILD_FAILURE(unsupported_builtin)); }
VPRIV val virbuild_wconst(ecx *e, i128 v) { (void)v; virbuild_fail(e, VIRBUILD_FAILURE(wide_integer)); }
VPRIV val virbuild_wbitint(ecx *e, val v, unsigned bits, bool sgn) { (void)v, (void)bits, (void)sgn; virbuild_fail(e, VIRBUILD_FAILURE(bit_precise_integer)); }
VPRIV val virbuild_bigconst(ecx *e, qty t, const uint32_t *words, i128 small) { (void)t, (void)words, (void)small; virbuild_fail(e, VIRBUILD_FAILURE(bit_precise_integer)); }
VPRIV val virbuild_bigbin(ecx *e, int op, qty t, val a, val b) { (void)op, (void)t, (void)a, (void)b; virbuild_fail(e, VIRBUILD_FAILURE(bit_precise_integer)); }
VPRIV val virbuild_bigcmp(ecx *e, int op, qty t, val a, val b) { (void)op, (void)t, (void)a, (void)b; virbuild_fail(e, VIRBUILD_FAILURE(bit_precise_integer)); }
VPRIV val virbuild_bigcvt(ecx *e, val v, qty from, qty to) { (void)v, (void)from, (void)to; virbuild_fail(e, VIRBUILD_FAILURE(bit_precise_integer)); }
VPRIV val virbuild_wbin(ecx *e, int op, bool sgn, val a, val b) { (void)op, (void)sgn, (void)a, (void)b; virbuild_fail(e, VIRBUILD_FAILURE(wide_integer)); }
VPRIV val virbuild_wun(ecx *e, int op, val a) { (void)op, (void)a; virbuild_fail(e, VIRBUILD_FAILURE(wide_integer)); }
VPRIV val virbuild_wcmp(ecx *e, int op, bool sgn, val a, val b) { (void)op, (void)sgn, (void)a, (void)b; virbuild_fail(e, VIRBUILD_FAILURE(wide_integer)); }
VPRIV val virbuild_wcvt(ecx *e, val v, qty from, qty to) { (void)v, (void)from, (void)to; virbuild_fail(e, VIRBUILD_FAILURE(wide_integer)); }

/* ---------------- floating point ---------------- */

VPRIV uint32_t virbuild_float_type(ecx *e, qty t)
{
	int k = KIND(e->c, t);

	if (k == TY_FLOAT)
		return VIR_TYPE_F32;
	if (k == TY_DOUBLE || (k == TY_LDOUBLE && tsize(e->c, t) == 8))
		return VIR_TYPE_F64;
	virbuild_fail(e, VIRBUILD_FAILURE(unsupported_floating_type));
}

static int virbuild_float_result_register(ecx *e, const val *a, const val *b)
{
	int r = a->k == V_REG && a->r >= VIRBUILD_FLOAT_REGISTER_FIRST ? a->r : virbuild_ftreg(e);

	if (b && b->k == V_REG && b->r != r)
		virbuild_tfree(e, b->r);
	return r;
}

VPRIV val virbuild_fconst(ecx *e, uint64_t bits, int size)
{
	int r = virbuild_ftreg(e);

	virbuild_register_write(e, r, virbuild_constant_make(e, VIR_OP_FCONST, size == 4 ? VIR_TYPE_F32 : VIR_TYPE_F64, 0, (uint32_t)bits, size == 4 ? 0 : (uint32_t)(bits >> 32)));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* a is the product an expression just made (in its name still), or that negated: the fmul, else 0.
 * *fl: VIR_INSN_FLAG_NEGP when negated. */
static uint32_t virbuild_float_product(ecx *e, const val *a, uint32_t x, uint32_t ty, uint32_t *fl)
{
	virbuild_context *V = e->vw;
	uint32_t m = x;

	*fl = 0;
	/* (arm64 only: the one lowering with fmadd) */
	if (e->c->o->no_fp_contract || e->c->o->target > PP_DARWIN || a->k != V_REG || a->r < VIRBUILD_FLOAT_REGISTER_FIRST ||
	    V->float_products[a->r - VIRBUILD_FLOAT_REGISTER_FIRST] != x || !x)
		return 0;
	if (V->insns[m].insn.op == VIR_OP_FNEG)
		*fl |= VIR_INSN_FLAG_NEGP, m = V->insns[m].insn.a;
	if (V->insns[m].insn.op != VIR_OP_FMUL || V->insns[m].insn.type != ty)
		return 0;
	if (V->insns[V->insns[m].insn.a].insn.op == VIR_OP_FCONST && V->insns[V->insns[m].insn.b].insn.op == VIR_OP_FCONST)
		return 0;	/* (a constant product is rounded on its own) */
	return m;
}

VPRIV val virbuild_fbin(ecx *e, int op, qty t, val a, val b)
{
	uint32_t ty = virbuild_float_type(e, t), x = virbuild_walker_value_get(e, &a, ty), y = virbuild_walker_value_get(e, &b, ty);
	int r = virbuild_float_result_register(e, &a, &b);
	uint32_t vop = op == LEX_TOKEN_PLUS ? VIR_OP_FADD : op == LEX_TOKEN_MINUS ? VIR_OP_FSUB : op == LEX_TOKEN_STAR ? VIR_OP_FMUL : VIR_OP_FDIV;
	uint32_t v, m = 0, fl = 0;
	bool left = true;

	if (vop <= VIR_OP_FSUB && !(m = virbuild_float_product(e, &a, x, ty, &fl)))
		left = false, m = virbuild_float_product(e, &b, y, ty, &fl);
	if (m) {
		/* a product this expression made, added or subtracted: one fused operation, as clang's
		 * -ffp-contract=on (the left operand first; the product itself goes when nothing else reads it) */
		const virbuild_preir_insn *P = e->vw->insns;
		uint32_t pa = P[m].insn.a, pb = P[m].insn.b, c = left ? y : x;

		if (vop == VIR_OP_FSUB)
			fl ^= left ? VIR_INSN_FLAG_NEGC : VIR_INSN_FLAG_NEGP;
		/* a negated factor or addend is a flag (exact) */
		if (P[pa].insn.op == VIR_OP_FNEG)
			fl ^= VIR_INSN_FLAG_NEGP, pa = P[pa].insn.a;
		if (P[pb].insn.op == VIR_OP_FNEG)
			fl ^= VIR_INSN_FLAG_NEGP, pb = P[pb].insn.a;
		if (P[c].insn.op == VIR_OP_FNEG)
			fl ^= VIR_INSN_FLAG_NEGC, c = P[c].insn.a;
		v = virbuild_insn_append(e, VIR_OP_FMA, ty, fl, pa, pb, c);
	} else {
		v = virbuild_insn_append(e, vop, ty, 0, x, y, 0);
	}
	virbuild_register_write(e, r, v);
	if (vop == VIR_OP_FMUL)
		e->vw->float_products[r - VIRBUILD_FLOAT_REGISTER_FIRST] = v;
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val virbuild_fneg(ecx *e, qty t, val a)
{
	uint32_t ty = virbuild_float_type(e, t), x = virbuild_walker_value_get(e, &a, ty);
	int r = virbuild_float_result_register(e, &a, NULL);

	bool prod = a.k == V_REG && a.r >= VIRBUILD_FLOAT_REGISTER_FIRST && x && e->vw->float_products[a.r - VIRBUILD_FLOAT_REGISTER_FIRST] == x && e->vw->insns[x].insn.op == VIR_OP_FMUL;
	uint32_t v = virbuild_insn_append(e, VIR_OP_FNEG, ty, 0, x, 0, 0);

	virbuild_register_write(e, r, v);
	if (prod)
		e->vw->float_products[r - VIRBUILD_FLOAT_REGISTER_FIRST] = v;
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val virbuild_fsqrt(ecx *e, qty t, val a)
{
	uint32_t ty = virbuild_float_type(e, t), x = virbuild_walker_value_get(e, &a, ty);
	int r = virbuild_float_result_register(e, &a, NULL);

	virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_FSQRT, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val virbuild_root4(ecx *e, val a) { (void)a; virbuild_fail(e, VIRBUILD_FAILURE(root4)); }
VPRIV val virbuild_pcmpstr(ecx *e, uint32_t n) { (void)n; virbuild_fail(e, VIRBUILD_FAILURE(string_compare)); }

VPRIV val virbuild_fcmp(ecx *e, int op, qty t, val a, val b)
{
	uint32_t ty = virbuild_float_type(e, t), x = virbuild_walker_value_get(e, &a, ty), y = virbuild_walker_value_get(e, &b, ty), cc;
	int r;

	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: cc = 0; break;
	case LEX_TOKEN_EXCLAMATION_EQUAL: cc = 1; break;
	case LEX_TOKEN_LESS: cc = 2; break;
	case LEX_TOKEN_LESS_EQUAL: cc = 3; break;
	case LEX_TOKEN_GREATER: cc = 4; break;
	case LEX_TOKEN_GREATER_EQUAL: cc = 5; break;
	case WK_FCMP_UNORDERED: cc = 6; break;
	case WK_FCMP_ORDERED_NE: cc = 7; break;
	default: virbuild_fail(e, VIRBUILD_FAILURE(unsupported_float_comparison));
	}
	if (a.k == V_REG) virbuild_tfree(e, a.r);
	if (b.k == V_REG) virbuild_tfree(e, b.r);
	r = virbuild_treg(e);
	virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_FCMP, VIR_TYPE_I32, cc, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val virbuild_cvt(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool ff = is_flt(c, from), tf = is_flt(c, to);
	int fs = wk_size(c, from), ts = wk_size(c, to);
	uint32_t x;
	int r;

	if (ff && tf) {
		uint32_t a = virbuild_float_type(e, from), b = virbuild_float_type(e, to);

		x = virbuild_walker_value_get(e, &v, a);
		if (a != b)
			x = virbuild_insn_append(e, a == VIR_TYPE_F32 ? VIR_OP_FEXT : VIR_OP_FTRUNC, b, 0, x, 0, 0);
		r = virbuild_float_result_register(e, &v, NULL);
		virbuild_register_write(e, r, x);
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	if (tf) {	/* integer to float */
		bool sg = wk_signed(c, from);
		uint32_t b = virbuild_float_type(e, to), it = fs == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32;

		x = virbuild_walker_value_get(e, &v, fs == 8 ? VIR_TYPE_I64 : (fs < 4 || sg) ? VIR_TYPE_I64 : VIR_TYPE_I32);
		if (fs < 8 && sg) {	/* the low fs bytes, signed: as i32 */
			x = virbuild_insn_append(e, VIR_OP_EXT, VIR_TYPE_I64, virbuild_integer_log2((uint64_t)fs) | VIR_INSN_FLAG_MS, x, 0, 0);
			x = virbuild_value_convert(e, x, VIR_TYPE_I32);
			it = VIR_TYPE_I32;
		} else if (fs < 4) {
			x = virbuild_value_convert(e, virbuild_insn_append(e, VIR_OP_EXT, VIR_TYPE_I64, virbuild_integer_log2((uint64_t)fs), x, 0, 0), VIR_TYPE_I32);
			it = VIR_TYPE_I32;
		}
		(void)it;
		if (v.k == V_REG && v.r < VIRBUILD_FLOAT_REGISTER_FIRST)
			virbuild_tfree(e, v.r);
		r = virbuild_ftreg(e);
		virbuild_register_write(e, r, virbuild_insn_append(e, sg ? VIR_OP_S2F : VIR_OP_U2F, b, 0, x, 0, 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	if (KIND(c, to) == TY_BOOL) {
		uint32_t a = virbuild_float_type(e, from);

		x = virbuild_walker_value_get(e, &v, a);
		if (v.k == V_REG) virbuild_tfree(e, v.r);
		r = virbuild_treg(e);
		virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_FCMP, VIR_TYPE_I32, 1, x, virbuild_constant_make(e, VIR_OP_FCONST, a, 0, 0, 0), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	{	/* float to integer */
		uint32_t a = virbuild_float_type(e, from), it = ts == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32;
		bool sg = wk_signed(c, to);

		x = virbuild_walker_value_get(e, &v, a);
		if (v.k == V_REG) virbuild_tfree(e, v.r);
		r = virbuild_treg(e);
		x = virbuild_insn_append(e, sg ? VIR_OP_F2S : VIR_OP_F2U, it, 0, x, 0, 0);
		virbuild_register_write(e, r, x);
		if (ts < 4)
			virbuild_ext(e, r, ts, sg);
		else if (ts == 4 && sg)
			virbuild_ext(e, r, 4, true);
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
}

VPRIV val virbuild_fbits(ecx *e, val v, int size)
{
	uint32_t x = virbuild_walker_value_get(e, &v, size == 4 ? VIR_TYPE_F32 : VIR_TYPE_F64);
	int r;

	if (v.k == V_REG) virbuild_tfree(e, v.r);
	r = virbuild_treg(e);
	virbuild_register_write(e, r, virbuild_insn_append(e, VIR_OP_BITCAST, size == 4 ? VIR_TYPE_I32 : VIR_TYPE_I64, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}
