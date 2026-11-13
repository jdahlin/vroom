/* The walker's lowering interface for values: conversions, registers, integer and floating point operations. */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* ---------------- conversions ---------------- */

VPRIV uint32_t vw_conv_in(ecx *e, uint32_t b, uint32_t v, uint32_t to)
{
	vwst *V = e->vw;
	uint32_t from = V->p[v].in.type;

	if (from == to)
		return v;
	if (V->p[v].in.op == VIR_OP_ICONST && to != VIR_TYPE_F32 && to != VIR_TYPE_F64) {
		uint64_t x = V->p[v].in.b | (uint64_t)V->p[v].in.c << 32;

		return vw_iconst(e, to, to == VIR_TYPE_I32 ? (int64_t)(uint32_t)x : (int64_t)x);
	}
#define I(op, t, fl, a) vw_ins_in(e, b, op, t, fl, a, 0, 0)
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
	vw_fail(e, VIRFAIL(value_type_mismatch));
}

VPRIV uint32_t vw_conv(ecx *e, uint32_t v, uint32_t to) { return vw_conv_in(e, e->vw->cur, v, to); }

/* name r's value as type ty */
VPRIV uint32_t vw_get(ecx *e, int r, uint32_t ty)
{
	if (r < 0 || r >= VW_NONAME)
		vw_fail(e, VIRFAIL(no_such_register));
	return vw_conv(e, vw_readv(e, (uint32_t)r, e->vw->cur, ty), ty);
}

VPRIV uint32_t vw_getraw(ecx *e, int r)	/* name r's value as it is */
{
	vwst *V = e->vw;
	uint32_t *d = vw_hslot(e, V->cur, (uint32_t)r, false);

	if (d)
		return *d;
	return vw_readv(e, (uint32_t)r, V->cur, r >= VW_FNAME0 ? VIR_TYPE_F64 : VIR_TYPE_I64);
}

VPRIV void vw_set(ecx *e, int r, uint32_t v)
{
	if (r < 0 || r >= VW_NONAME)
		vw_fail(e, VIRFAIL(no_such_register));
	if (r >= VW_FNAME0)
		e->vw->fprod[r - VW_FNAME0] = 0;
	vw_write(e, (uint32_t)r, e->vw->cur, v);
}

VPRIV uint32_t vw_ity(int size) { return size == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32; }

/* a walker value as a VIR value of type ty */
VPRIV uint32_t vw_val(ecx *e, const val *v, uint32_t ty)
{
	switch (v->k) {
	case V_CONST:
		if (ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64)
			vw_fail(e, VIRFAIL(integer_constant_as_float));
		return vw_iconst(e, ty, (int64_t)v->i);
	case V_ADDR: {
		uint32_t a = vw_k(e, VIR_OP_SYMADDR, VIR_TYPE_PTR, v->ref, (uint32_t)(uint64_t)v->i, (uint32_t)((uint64_t)v->i >> 32));

		note_ref(e, v->ref);
		return vw_conv(e, a, ty);
	}
	case V_REG:
		return vw_get(e, v->r, ty);
	default:
		vw_fail(e, VIRFAIL(unsupported_value_kind));
	}
}

/* the type a value of the walker's has (V_REG: its name's current value's) */
static uint32_t vw_vty(ecx *e, const val *v)
{
	if (v->k == V_ADDR)
		return VIR_TYPE_PTR;
	if (v->k == V_REG)
		return vw_ty(e, vw_getraw(e, v->r));
	return VIR_TYPE_I64;
}

/* ---------------- registers ---------------- */

VPRIV int vw_treg(ecx *e)
{
	vwst *V = e->vw;

	for (int w = 0; w < 3; w++)
		if (~V->live[w]) {
			int k = __builtin_ctzll(~V->live[w]);

			V->live[w] |= 1ull << k;
			return w * 64 + k;
		}
	vw_fail(e, VIRFAIL(out_of_names));
}

VPRIV int vw_ftreg(ecx *e)
{
	vwst *V = e->vw;

	for (int r = VW_FNAME0; r < VW_NONAME; r++)
		if (!(V->flive >> (r - VW_FNAME0) & 1)) {
			V->flive |= 1ull << (r - VW_FNAME0);
			return r;
		}
	vw_fail(e, VIRFAIL(out_of_float_names));
}

VPRIV void vw_tfree(ecx *e, int r)
{
	vwst *V = e->vw;

	if (r >= 0 && r < VW_FNAME0)
		V->live[r / 64] &= ~(1ull << (r % 64));
	else if (r >= VW_FNAME0 && r < VW_NONAME)
		V->flive &= ~(1ull << (r - VW_FNAME0));
}

VPRIV void vw_tkeep(ecx *e, int r)
{
	vwst *V = e->vw;

	if (r >= 0 && r < VW_FNAME0)
		V->live[r / 64] |= 1ull << (r % 64);
	else if (r >= VW_FNAME0 && r < VW_NONAME)
		V->flive |= 1ull << (r - VW_FNAME0);
}

VPRIV int vw_nfree(ecx *e)
{
	vwst *V = e->vw;

	return 192 - __builtin_popcountll(V->live[0]) - __builtin_popcountll(V->live[1]) - __builtin_popcountll(V->live[2]);
}

VPRIV int vw_nffree(ecx *e) { return 63 - __builtin_popcountll(e->vw->flive); }

VPRIV void vw_mov(ecx *e, int rd, int rs)
{
	if (rd == rs)
		return;
	vw_set(e, rd, vw_getraw(e, rs));
}

VPRIV int vw_toreg(ecx *e, val *v, int size)
{
	int r;

	(void)size;
	if (v->k == V_REG)
		return v->r;
	r = vw_treg(e);
	vw_set(e, r, vw_val(e, v, v->k == V_ADDR ? VIR_TYPE_PTR : VIR_TYPE_I64));
	*v = (val){ .k = V_REG, .r = (uint8_t)r };
	return r;
}

/* a result's name: a's if a is in an integer name, else a new one; b's freed */
VPRIV int vw_resname(ecx *e, const val *a, const val *b)
{
	int r = a->k == V_REG && a->r < VW_FNAME0 ? a->r : vw_treg(e);

	if (b && b->k == V_REG && b->r != r)
		vw_tfree(e, b->r);
	return r;
}

/* ---------------- integer operations ---------------- */

VPRIV val vw_umulhi(ecx *e, val a, val b)
{
	uint32_t x = vw_val(e, &a, VIR_TYPE_I64), y = vw_val(e, &b, VIR_TYPE_I64);
	int r = vw_resname(e, &a, &b);

	vw_set(e, r, vw_ins(e, VIR_OP_UMULHI, VIR_TYPE_I64, 0, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_smulhi(ecx *e, val a, val b)
{
	uint32_t x = vw_val(e, &a, VIR_TYPE_I64), y = vw_val(e, &b, VIR_TYPE_I64);
	int r = vw_resname(e, &a, &b);

	vw_set(e, r, vw_ins(e, VIR_OP_SMULHI, VIR_TYPE_I64, 0, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_bin(ecx *e, int op, int size, bool sgn, val a, val b)
{
	uint32_t ty = vw_ity(size), vop, x, y, at, bt;
	int r;

	at = vw_vty(e, &a), bt = vw_vty(e, &b);
	if (size == 8 && (op == LEX_TOKEN_PLUS || op == LEX_TOKEN_MINUS) && (at == VIR_TYPE_PTR || bt == VIR_TYPE_PTR)) {
		/* pointer arithmetic */
		if (op == LEX_TOKEN_PLUS && at != VIR_TYPE_PTR) {
			val t = a;

			a = b, b = t;
			at = VIR_TYPE_PTR, bt = vw_vty(e, &b);
		}
		if (at == VIR_TYPE_PTR && !(op == LEX_TOKEN_MINUS && bt == VIR_TYPE_PTR)) {
			x = vw_val(e, &a, VIR_TYPE_PTR);
			y = vw_val(e, &b, VIR_TYPE_I64);
			if (op == LEX_TOKEN_MINUS)
				y = b.k == V_CONST ? vw_iconst(e, VIR_TYPE_I64, -(int64_t)b.i) : vw_ins(e, VIR_OP_NEG, VIR_TYPE_I64, 0, y, 0, 0);
			r = vw_resname(e, &a, &b);
			vw_set(e, r, vw_ins(e, VIR_OP_PTRADD, VIR_TYPE_PTR, 0, x, y, 0));
			return (val){ .k = V_REG, .r = (uint8_t)r };
		}
		if (op == LEX_TOKEN_MINUS && at == VIR_TYPE_PTR && bt == VIR_TYPE_PTR) {
			x = vw_val(e, &a, VIR_TYPE_PTR);
			y = vw_val(e, &b, VIR_TYPE_PTR);
			r = vw_resname(e, &a, &b);
			vw_set(e, r, vw_ins(e, VIR_OP_PSUB, VIR_TYPE_I64, 0, x, y, 0));
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
	default: vw_fail(e, VIRFAIL(unsupported_operator));
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
	x = vw_val(e, &a, ty);
	y = vw_val(e, &b, ty);
	r = vw_resname(e, &a, &b);
	/* signed +, -, *: overflow is undefined (not under -fwrapv), which is what makes an index linear */
	vw_set(e, r, vw_ins(e, vop, ty, fl, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_un(ecx *e, int op, int size, val a)
{
	uint32_t ty = vw_ity(size), x = vw_val(e, &a, ty);
	int r = vw_resname(e, &a, NULL);

	vw_set(e, r, vw_ins(e, op == LEX_TOKEN_MINUS ? VIR_OP_NEG : VIR_OP_NOT, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV void vw_ext(ecx *e, int r, int from, bool sgn)
{
	uint32_t v;

	if (from >= 8)
		return;
	v = vw_getraw(e, r);
	if (e->vw->p[v].in.op == VIR_OP_ICONST) {	/* a constant: folded */
		uint64_t x = e->vw->p[v].in.b | (uint64_t)e->vw->p[v].in.c << 32;
		int sh = 64 - 8 * from;

		x = sgn ? (uint64_t)((int64_t)(x << sh) >> sh) : x << sh >> sh;
		vw_set(e, r, vw_iconst(e, !sgn && from == 4 ? VIR_TYPE_I32 : VIR_TYPE_I64, (int64_t)x));
		return;
	}
	if (from == 4 && !sgn) {
		vw_set(e, r, vw_conv(e, v, VIR_TYPE_I32));
		return;
	}
	if (vw_ty(e, v) == VIR_TYPE_PTR)
		v = vw_conv(e, v, VIR_TYPE_I64);
	if (vw_ty(e, v) == VIR_TYPE_I32 && from == 4) {	/* sign extension of an i32 */
		vw_set(e, r, vw_ins(e, VIR_OP_EXT, VIR_TYPE_I64, 2 | VIR_INSN_FLAG_MS, v, 0, 0));
		return;
	}
	if (vw_ty(e, v) == VIR_TYPE_F32 || vw_ty(e, v) == VIR_TYPE_F64)
		vw_fail(e, VIRFAIL(float_extension));
	/* narrow: the result as wide as the operand (an i32 with a zero upper half, or i64 for a sign) */
	if (sgn)
		v = vw_conv(e, v, VIR_TYPE_I64);
	vw_set(e, r, vw_ins(e, VIR_OP_EXT, vw_ty(e, v), vw_log2((uint64_t)from) | (sgn ? VIR_INSN_FLAG_MS : 0), v, 0, 0));
}

static uint32_t vw_icc(int op, bool sgn)
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

static uint32_t vw_icmp(ecx *e, int op, int size, bool sgn, val *a, val *b)
{
	uint32_t ty = vw_ity(size), x, y;

	if (size == 8 && !sgn && vw_vty(e, a) == VIR_TYPE_PTR && vw_vty(e, b) == VIR_TYPE_PTR)
		ty = VIR_TYPE_PTR;
	x = vw_val(e, a, ty);
	y = vw_val(e, b, ty);
	return vw_ins(e, VIR_OP_ICMP, VIR_TYPE_I32, vw_icc(op, sgn), x, y, 0);
}

VPRIV val vw_cmp(ecx *e, int op, int size, bool sgn, val a, val b)
{
	uint32_t c = vw_icmp(e, op, size, sgn, &a, &b);
	int r = vw_resname(e, &a, &b);

	vw_set(e, r, c);
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* to lbl if c (an i32 or i64) is nonzero, else on */
static void vw_brif(ecx *e, uint32_t c, uint32_t lbl, bool nz)
{
	vwst *V = e->vw;
	uint32_t t = vw_lblk(e, lbl), f = vw_newblk(e);

	V->b[V->cur].term = vw_put(e, VIR_OP_BRIF, VIR_TYPE_VOID, 0, c, nz ? t : f, nz ? f : t, V->cur);
	V->cur = f;
	/* (the fallthrough's only predecessor is this block: the store table stays valid) */
	V->b[f].fall = 1;
}

VPRIV void vw_cmpbr(ecx *e, int op, int size, bool sgn, val a, val b, uint32_t lbl)
{
	uint32_t c = vw_icmp(e, op, size, sgn, &a, &b);

	if (a.k == V_REG) vw_tfree(e, a.r);
	if (b.k == V_REG) vw_tfree(e, b.r);
	vw_brif(e, c, lbl, true);
}

VPRIV void vw_brz(ecx *e, val a, int size, bool nz, uint32_t lbl)
{
	uint32_t t = vw_vty(e, &a), c;

	if (t == VIR_TYPE_PTR)
		c = vw_ins(e, VIR_OP_ICMP, VIR_TYPE_I32, 1, vw_val(e, &a, VIR_TYPE_PTR), vw_iconst(e, VIR_TYPE_PTR, 0), 0);
	else
		c = vw_val(e, &a, vw_ity(size));
	if (a.k == V_REG) vw_tfree(e, a.r);
	vw_brif(e, c, lbl, nz);
}

VPRIV void vw_switch_bitset(ecx *e, int r, int size, const em_switch_bitset_recipe *recipe, uint32_t target, uint32_t dflt)
{
	(void)r, (void)size, (void)recipe, (void)target, (void)dflt;
	vw_fail(e, VIRFAIL(switch_bit_set));
}

static int vw_casecmp(const void *a, const void *b)
{
	const uint64_t *x = a, *y = b;

	return x[0] < y[0] ? -1 : x[0] > y[0];
}

VPRIV void vw_switch_table(ecx *e, int r, int size, const em_switch_case *cases, uint32_t n, uint32_t dflt)
{
	vwst *V = e->vw;
	uint32_t ty = vw_ity(size), x = vw_get(e, r, ty), st = size == 8 ? 5 : 3, m = 0;
	uint64_t mask = size == 8 ? ~0ull : 0xffffffffull, *c3 = malloc((size_t)(2 * n + 1) * 3 * sizeof *c3);
	uint32_t o;

	if (!c3)
		vw_fail(e, VIRFAIL(out_of_memory));
	/* VIR's cases are unsigned and sorted: a range across the sign splits in two */
	for (uint32_t k = 0; k < n; k++) {
		uint64_t lo = (uint64_t)cases[k].lo & mask, hi = (uint64_t)cases[k].hi & mask, t = vw_lblk(e, cases[k].target);

		if (lo > hi) {
			c3[3 * m] = lo, c3[3 * m + 1] = mask, c3[3 * m + 2] = t, m++;
			lo = 0;
		}
		c3[3 * m] = lo, c3[3 * m + 1] = hi, c3[3 * m + 2] = t, m++;
	}
	qsort(c3, m, 3 * sizeof *c3, vw_casecmp);
	o = vw_xput(e, &m, 1);
	GROW(V->x, V->nx, V->cx, m * st);
	for (uint32_t k = 0; k < m; k++) {
		uint64_t lo = c3[3 * k], hi = c3[3 * k + 1];
		uint32_t *w = &V->x[V->nx];

		if (size == 8) {
			w[0] = (uint32_t)lo, w[1] = (uint32_t)(lo >> 32), w[2] = (uint32_t)hi, w[3] = (uint32_t)(hi >> 32);
		} else {
			w[0] = (uint32_t)lo, w[1] = (uint32_t)hi;
		}
		w[st - 1] = (uint32_t)c3[3 * k + 2];
		V->nx += st;
	}
	free(c3);
	vw_term(e, VIR_OP_SWITCH, x, vw_lblk(e, dflt), o, 0);
	e->reach = false;
}

/* __int128 and _BitInt: not in 1.0 (SPEC 19) */
VPRIV bool vw_ldmem(void) { return true; }
VPRIV bool vw_i128mem(void) { return true; }
#define VW_NO(name, ...) VPRIV val vw_##name(ecx *e, __VA_ARGS__) { vw_fail(e, VIRFAIL(unsupported_builtin)); }
VPRIV val vw_wconst(ecx *e, i128 v) { (void)v; vw_fail(e, VIRFAIL(wide_integer)); }
VPRIV val vw_wbitint(ecx *e, val v, unsigned bits, bool sgn) { (void)v, (void)bits, (void)sgn; vw_fail(e, VIRFAIL(bit_precise_integer)); }
VPRIV val vw_bigconst(ecx *e, qty t, const uint32_t *words, i128 small) { (void)t, (void)words, (void)small; vw_fail(e, VIRFAIL(bit_precise_integer)); }
VPRIV val vw_bigbin(ecx *e, int op, qty t, val a, val b) { (void)op, (void)t, (void)a, (void)b; vw_fail(e, VIRFAIL(bit_precise_integer)); }
VPRIV val vw_bigcmp(ecx *e, int op, qty t, val a, val b) { (void)op, (void)t, (void)a, (void)b; vw_fail(e, VIRFAIL(bit_precise_integer)); }
VPRIV val vw_bigcvt(ecx *e, val v, qty from, qty to) { (void)v, (void)from, (void)to; vw_fail(e, VIRFAIL(bit_precise_integer)); }
VPRIV val vw_wbin(ecx *e, int op, bool sgn, val a, val b) { (void)op, (void)sgn, (void)a, (void)b; vw_fail(e, VIRFAIL(wide_integer)); }
VPRIV val vw_wun(ecx *e, int op, val a) { (void)op, (void)a; vw_fail(e, VIRFAIL(wide_integer)); }
VPRIV val vw_wcmp(ecx *e, int op, bool sgn, val a, val b) { (void)op, (void)sgn, (void)a, (void)b; vw_fail(e, VIRFAIL(wide_integer)); }
VPRIV val vw_wcvt(ecx *e, val v, qty from, qty to) { (void)v, (void)from, (void)to; vw_fail(e, VIRFAIL(wide_integer)); }

/* ---------------- floating point ---------------- */

VPRIV uint32_t vw_fty(ecx *e, qty t)
{
	int k = KIND(e->c, t);

	if (k == TY_FLOAT)
		return VIR_TYPE_F32;
	if (k == TY_DOUBLE || (k == TY_LDOUBLE && tsize(e->c, t) == 8))
		return VIR_TYPE_F64;
	vw_fail(e, VIRFAIL(unsupported_floating_type));
}

static int vw_fres(ecx *e, const val *a, const val *b)
{
	int r = a->k == V_REG && a->r >= VW_FNAME0 ? a->r : vw_ftreg(e);

	if (b && b->k == V_REG && b->r != r)
		vw_tfree(e, b->r);
	return r;
}

VPRIV val vw_fconst(ecx *e, uint64_t bits, int size)
{
	int r = vw_ftreg(e);

	vw_set(e, r, vw_k(e, VIR_OP_FCONST, size == 4 ? VIR_TYPE_F32 : VIR_TYPE_F64, 0, (uint32_t)bits, size == 4 ? 0 : (uint32_t)(bits >> 32)));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

/* a is the product an expression just made (in its name still), or that negated: the fmul, else 0.
 * *fl: VIR_INSN_FLAG_NEGP when negated. */
static uint32_t vw_fprod(ecx *e, const val *a, uint32_t x, uint32_t ty, uint32_t *fl)
{
	vwst *V = e->vw;
	uint32_t m = x;

	*fl = 0;
	/* (arm64 only: the one lowering with fmadd) */
	if (e->c->o->no_fp_contract || e->c->o->target > PP_DARWIN || a->k != V_REG || a->r < VW_FNAME0 ||
	    V->fprod[a->r - VW_FNAME0] != x || !x)
		return 0;
	if (V->p[m].in.op == VIR_OP_FNEG)
		*fl |= VIR_INSN_FLAG_NEGP, m = V->p[m].in.a;
	if (V->p[m].in.op != VIR_OP_FMUL || V->p[m].in.type != ty)
		return 0;
	if (V->p[V->p[m].in.a].in.op == VIR_OP_FCONST && V->p[V->p[m].in.b].in.op == VIR_OP_FCONST)
		return 0;	/* (a constant product is rounded on its own) */
	return m;
}

VPRIV val vw_fbin(ecx *e, int op, qty t, val a, val b)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty), y = vw_val(e, &b, ty);
	int r = vw_fres(e, &a, &b);
	uint32_t vop = op == LEX_TOKEN_PLUS ? VIR_OP_FADD : op == LEX_TOKEN_MINUS ? VIR_OP_FSUB : op == LEX_TOKEN_STAR ? VIR_OP_FMUL : VIR_OP_FDIV;
	uint32_t v, m = 0, fl = 0;
	bool left = true;

	if (vop <= VIR_OP_FSUB && !(m = vw_fprod(e, &a, x, ty, &fl)))
		left = false, m = vw_fprod(e, &b, y, ty, &fl);
	if (m) {
		/* a product this expression made, added or subtracted: one fused operation, as clang's
		 * -ffp-contract=on (the left operand first; the product itself goes when nothing else reads it) */
		const vwp *P = e->vw->p;
		uint32_t pa = P[m].in.a, pb = P[m].in.b, c = left ? y : x;

		if (vop == VIR_OP_FSUB)
			fl ^= left ? VIR_INSN_FLAG_NEGC : VIR_INSN_FLAG_NEGP;
		/* a negated factor or addend is a flag (exact) */
		if (P[pa].in.op == VIR_OP_FNEG)
			fl ^= VIR_INSN_FLAG_NEGP, pa = P[pa].in.a;
		if (P[pb].in.op == VIR_OP_FNEG)
			fl ^= VIR_INSN_FLAG_NEGP, pb = P[pb].in.a;
		if (P[c].in.op == VIR_OP_FNEG)
			fl ^= VIR_INSN_FLAG_NEGC, c = P[c].in.a;
		v = vw_ins(e, VIR_OP_FMA, ty, fl, pa, pb, c);
	} else {
		v = vw_ins(e, vop, ty, 0, x, y, 0);
	}
	vw_set(e, r, v);
	if (vop == VIR_OP_FMUL)
		e->vw->fprod[r - VW_FNAME0] = v;
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_fneg(ecx *e, qty t, val a)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty);
	int r = vw_fres(e, &a, NULL);

	bool prod = a.k == V_REG && a.r >= VW_FNAME0 && x && e->vw->fprod[a.r - VW_FNAME0] == x && e->vw->p[x].in.op == VIR_OP_FMUL;
	uint32_t v = vw_ins(e, VIR_OP_FNEG, ty, 0, x, 0, 0);

	vw_set(e, r, v);
	if (prod)
		e->vw->fprod[r - VW_FNAME0] = v;
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_fsqrt(ecx *e, qty t, val a)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty);
	int r = vw_fres(e, &a, NULL);

	vw_set(e, r, vw_ins(e, VIR_OP_FSQRT, ty, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_root4(ecx *e, val a) { (void)a; vw_fail(e, VIRFAIL(root4)); }
VPRIV val vw_pcmpstr(ecx *e, uint32_t n) { (void)n; vw_fail(e, VIRFAIL(string_compare)); }

VPRIV val vw_fcmp(ecx *e, int op, qty t, val a, val b)
{
	uint32_t ty = vw_fty(e, t), x = vw_val(e, &a, ty), y = vw_val(e, &b, ty), cc;
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
	default: vw_fail(e, VIRFAIL(unsupported_float_comparison));
	}
	if (a.k == V_REG) vw_tfree(e, a.r);
	if (b.k == V_REG) vw_tfree(e, b.r);
	r = vw_treg(e);
	vw_set(e, r, vw_ins(e, VIR_OP_FCMP, VIR_TYPE_I32, cc, x, y, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}

VPRIV val vw_cvt(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool ff = is_flt(c, from), tf = is_flt(c, to);
	int fs = wk_size(c, from), ts = wk_size(c, to);
	uint32_t x;
	int r;

	if (ff && tf) {
		uint32_t a = vw_fty(e, from), b = vw_fty(e, to);

		x = vw_val(e, &v, a);
		if (a != b)
			x = vw_ins(e, a == VIR_TYPE_F32 ? VIR_OP_FEXT : VIR_OP_FTRUNC, b, 0, x, 0, 0);
		r = vw_fres(e, &v, NULL);
		vw_set(e, r, x);
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	if (tf) {	/* integer to float */
		bool sg = wk_signed(c, from);
		uint32_t b = vw_fty(e, to), it = fs == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32;

		x = vw_val(e, &v, fs == 8 ? VIR_TYPE_I64 : (fs < 4 || sg) ? VIR_TYPE_I64 : VIR_TYPE_I32);
		if (fs < 8 && sg) {	/* the low fs bytes, signed: as i32 */
			x = vw_ins(e, VIR_OP_EXT, VIR_TYPE_I64, vw_log2((uint64_t)fs) | VIR_INSN_FLAG_MS, x, 0, 0);
			x = vw_conv(e, x, VIR_TYPE_I32);
			it = VIR_TYPE_I32;
		} else if (fs < 4) {
			x = vw_conv(e, vw_ins(e, VIR_OP_EXT, VIR_TYPE_I64, vw_log2((uint64_t)fs), x, 0, 0), VIR_TYPE_I32);
			it = VIR_TYPE_I32;
		}
		(void)it;
		if (v.k == V_REG && v.r < VW_FNAME0)
			vw_tfree(e, v.r);
		r = vw_ftreg(e);
		vw_set(e, r, vw_ins(e, sg ? VIR_OP_S2F : VIR_OP_U2F, b, 0, x, 0, 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	if (KIND(c, to) == TY_BOOL) {
		uint32_t a = vw_fty(e, from);

		x = vw_val(e, &v, a);
		if (v.k == V_REG) vw_tfree(e, v.r);
		r = vw_treg(e);
		vw_set(e, r, vw_ins(e, VIR_OP_FCMP, VIR_TYPE_I32, 1, x, vw_k(e, VIR_OP_FCONST, a, 0, 0, 0), 0));
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
	{	/* float to integer */
		uint32_t a = vw_fty(e, from), it = ts == 8 ? VIR_TYPE_I64 : VIR_TYPE_I32;
		bool sg = wk_signed(c, to);

		x = vw_val(e, &v, a);
		if (v.k == V_REG) vw_tfree(e, v.r);
		r = vw_treg(e);
		x = vw_ins(e, sg ? VIR_OP_F2S : VIR_OP_F2U, it, 0, x, 0, 0);
		vw_set(e, r, x);
		if (ts < 4)
			vw_ext(e, r, ts, sg);
		else if (ts == 4 && sg)
			vw_ext(e, r, 4, true);
		return (val){ .k = V_REG, .r = (uint8_t)r };
	}
}

VPRIV val vw_fbits(ecx *e, val v, int size)
{
	uint32_t x = vw_val(e, &v, size == 4 ? VIR_TYPE_F32 : VIR_TYPE_F64);
	int r;

	if (v.k == V_REG) vw_tfree(e, v.r);
	r = vw_treg(e);
	vw_set(e, r, vw_ins(e, VIR_OP_BITCAST, size == 4 ? VIR_TYPE_I32 : VIR_TYPE_I64, 0, x, 0, 0));
	return (val){ .k = V_REG, .r = (uint8_t)r };
}
