/* arm64 lowering of VIR: addresses, loads and stores. */

#include "lowering-internal.h"

/* load or store at operand 0's address: lv for the symbol or frame cases, else base register */
VPRIV lval a64_vir_address_get(a64_vir_context *L, uint32_t w, int t)
{
	lval l = { 0 };
	uint32_t v = L->ov[0];

	if (L->opr[0] == VIR_REGISTER_NONE && a64_vir_value_is_constant(L->f, v) && L->f->insns[v].op == VIR_OP_SLOTADDR) {
		l.k = L_FRAME;
		l.off = L->soff[L->f->insns[v].a] + a64_vir_constant_bits(&(vir_insn){ .b = L->f->insns[v].b, .c = L->f->insns[v].c });
		return l;
	}
	if (L->opr[0] == VIR_REGISTER_NONE && a64_vir_value_is_constant(L->f, v) && L->f->insns[v].op == VIR_OP_SYMADDR) {
		l.k = L_SYM;
		l.ref = L->f->insns[v].a;
		l.off = a64_vir_constant_bits(&(vir_insn){ .b = L->f->insns[v].b, .c = L->f->insns[v].c });
		l.align = 1u << VIR_INSN_FLAG_ALIGN_LOG2(w);
		return l;
	}
	l.k = L_REG;
	if (L->pval && L->pval == v) {
		l.r = (uint8_t)L->pbase;
		l.off = L->poff;
		L->pval = 0;
		return l;
	}
	l.r = (uint8_t)a64_vir_operand_general_register(L, 0, t);
	return l;
}

/* a float register loaded from or stored to [base, #off] without a general register in between;
 * false when the offset has no such form */
static bool a64_vir_float_memory_access(a64_vir_context *L, bool load, bool d, int v, int base, int64_t off)
{
	int sz = d ? 8 : 4;

	if (base > 30 && base != SP)
		return false;
	if (base == SP)
		base = 31;
	if (off >= 0 && off % sz == 0 && off / sz < 4096)
		word(L->e, (load ? 0xbd400000u : 0xbd000000u) | (unsigned)d << 30 | (unsigned)(off / sz) << 10 | (unsigned)base << 5 | (unsigned)v);
	else if (off >= -256 && off <= 255)
		word(L->e, (load ? 0xbc400000u : 0xbc000000u) | (unsigned)d << 30 | ((unsigned)off & 0x1ffu) << 12 | (unsigned)base << 5 | (unsigned)v);
	else
		return false;
	return true;
}

VPRIV void a64_vir_load_lower(a64_vir_context *L, uint32_t i, const vir_insn *in, uint8_t res)
{
	ecx *e = L->e;
	int w = VIR_INSN_FLAG_WIDTH_LOG2(in->flags), fl = in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64;
	int ext = fl || w == 3 || !(in->flags & VIR_INSN_FLAG_MS) ? 0 : in->type == VIR_TYPE_I32 || L->lo32[i] ? (w == 2 ? 0 : 2) : 1;
	int rd = fl || res == VIR_REGISTER_NONE ? R_X16 : a64_vir_general_register(res);
	lval l;

	if (L->xval && L->xval == L->ov[0] && fl && res != VIR_REGISTER_NONE && !(in->flags & VIR_INSN_FLAG_VOL)) {
		word(e, 0xbc600800u | (unsigned)(w == 3) << 30 | (unsigned)L->xreg << 16 | (unsigned)L->xopt << 13 |
			(unsigned)(L->xsh != 0) << 12 | (unsigned)L->xbase << 5 | (unsigned)a64_vir_vector_register(res));	/* ldr s/d, [x, index] */
		L->xval = 0;
		return;
	}
	if (L->xval && L->xval == L->ov[0]) {
		ins *x = put(e, I_LDX);

		x->sz = (uint8_t)w, x->cond = (uint8_t)ext, x->rd = (uint8_t)rd, x->rn = (uint8_t)L->xbase;
		x->rm = (uint8_t)L->xreg, x->ra = L->xopt, x->sh = L->xsh;
		L->xval = 0;
		if (fl && res != VIR_REGISTER_NONE)
			word(e, f_in(in->type == VIR_TYPE_F64, a64_vir_vector_register(res), R_X16));
		return;
	}
	l = a64_vir_address_get(L, in->flags, R_X16);
	if (fl && res != VIR_REGISTER_NONE && !(in->flags & VIR_INSN_FLAG_VOL) && l.k == L_REG && a64_vir_float_memory_access(L, true, w == 3, a64_vir_vector_register(res), l.r, l.off))
		return;	/* (straight into the v register) */
	e->novtag += (in->flags & VIR_INSN_FLAG_VOL) != 0;	/* (a volatile slot is no slot to the record clean-up) */
	if (l.k == L_SYM && ext != 2) {
		load_n(e, rd, &l, 1 << w, ext);
	} else if (l.k == L_SYM) {
		addr_of_ref(e, R_X16, l.ref, l.off);
		mem(e, I_LD, w, ext, rd, R_X16, 0);
	} else if (fl) {	/* straight into the FP register (a result nobody reads: v31, never allocated) */
		mem(e, I_LDF, w, 0, res == VIR_REGISTER_NONE ? 31 : a64_vir_vector_register(res), l.k == L_FRAME ? FRAME : l.r, l.off);
		e->novtag -= (in->flags & VIR_INSN_FLAG_VOL) != 0;
		return;
	} else {
		mem(e, I_LD, w, ext, rd, l.k == L_FRAME ? FRAME : l.r, l.off);
		if ((in->flags & VIR_INSN_FLAG_VOL) && l.k != L_FRAME && e->reach && e->in[e->nin - 1].op == I_LD)
			e->in[e->nin - 1].ra = LD_VOLATILE;	/* (not merged with its neighbours: opt.c's fold) */
	}
	e->novtag -= (in->flags & VIR_INSN_FLAG_VOL) != 0;
	if (fl && res != VIR_REGISTER_NONE)
		word(e, f_in(in->type == VIR_TYPE_F64, a64_vir_vector_register(res), R_X16));
}

VPRIV void a64_vir_store_lower(a64_vir_context *L, const vir_insn *in)
{
	ecx *e = L->e;
	int w = VIR_INSN_FLAG_WIDTH_LOG2(in->flags);
	uint32_t v = L->ov[1];
	bool xa = L->xval && L->xval == L->ov[0];
	lval l = xa ? (lval){ .k = L_REG, .r = (uint8_t)L->xbase } : a64_vir_address_get(L, in->flags, R_X16);
	int base_t = l.k == L_SYM || (l.k == L_REG && l.r == R_X16);	/* the address needs x16 */
	int rs;

	if (L->opr[1] != VIR_REGISTER_NONE && a64_vir_register_is_float(L->opr[1]) && !(in->flags & VIR_INSN_FLAG_VOL)) {	/* straight from the v register */
		if (xa) {
			word(e, 0xbc200800u | (unsigned)(w == 3) << 30 | (unsigned)L->xreg << 16 | (unsigned)L->xopt << 13 |
				(unsigned)(L->xsh != 0) << 12 | (unsigned)L->xbase << 5 | (unsigned)a64_vir_vector_register(L->opr[1]));
			L->xval = 0;
			return;
		}
		if (l.k == L_REG && a64_vir_float_memory_access(L, false, w == 3, a64_vir_vector_register(L->opr[1]), l.r, l.off))
			return;
	}
	if (!xa && l.k != L_SYM && L->opr[1] != VIR_REGISTER_NONE && a64_vir_register_is_float(L->opr[1])) {	/* ... a frame slot, a volatile one, a far offset: the record */
		e->novtag += (in->flags & VIR_INSN_FLAG_VOL) != 0;
		mem(e, I_STF, w, 0, a64_vir_vector_register(L->opr[1]), l.k == L_FRAME ? FRAME : l.r, l.off);
		e->novtag -= (in->flags & VIR_INSN_FLAG_VOL) != 0;
		return;
	}
	if (L->opr[1] == VIR_REGISTER_NONE && a64_vir_value_is_zero(L->f, v))
		rs = ZR;
	else if (L->opr[1] != VIR_REGISTER_NONE && a64_vir_register_is_float(L->opr[1])) {
		rs = base_t ? R_X17 : R_X16;
		word(e, f_out(w == 3, rs, a64_vir_vector_register(L->opr[1])));
	} else {
		rs = a64_vir_operand_general_register(L, 1, base_t ? R_X17 : R_X16);
	}
	if (xa) {
		ins *x = put(e, I_STX);

		x->sz = (uint8_t)w, x->rd = (uint8_t)rs, x->rn = (uint8_t)L->xbase, x->rm = (uint8_t)L->xreg;
		x->ra = L->xopt, x->sh = L->xsh;
		L->xval = 0;
	} else if (l.k == L_SYM) {
		store_n(e, rs, &l, 0, 1 << w);
	} else {
		e->novtag += (in->flags & VIR_INSN_FLAG_VOL) != 0;
		mem(e, I_ST, w, 0, rs, l.k == L_FRAME ? FRAME : l.r, l.off);
		e->novtag -= (in->flags & VIR_INSN_FLAG_VOL) != 0;
	}
}

/* an address operand of memcpy/memset: base register and offset */
static int a64_vir_memory_block_address_get(a64_vir_context *L, uint32_t k, int t, int64_t *off)
{
	uint32_t v = L->ov[k];

	*off = 0;
	if (L->opr[k] == VIR_REGISTER_NONE && a64_vir_value_is_constant(L->f, v) && L->f->insns[v].op == VIR_OP_SLOTADDR) {
		*off = L->soff[L->f->insns[v].a] + a64_vir_constant_bits(&(vir_insn){ .b = L->f->insns[v].b, .c = L->f->insns[v].c });
		return FRAME;
	}
	return a64_vir_operand_general_register(L, k, t);
}

VPRIV void a64_vir_memory_block_lower(a64_vir_context *L, const vir_insn *in)
{
	ecx *e = L->e;
	uint64_t n = (uint64_t)a64_vir_constant_bits(&L->f->insns[in->c]);

	if (a64_vir_insn_is_big_memory(L->f, in)) {
		e->calls = true;
		put(e, I_BL)->ref = FIXED(in->op == VIR_OP_MEMCPY ? FN_MEMCPY : FN_MEMSET);
		return;
	}
	if (in->op == VIR_OP_MEMSET && !a64_vir_value_is_zero(L->f, in->b))
		a64_vir_fail(L, VIRBUILD_FAILURE(memset_nonzero_byte));
	{
		int64_t doff, soff = 0;
		int d = a64_vir_memory_block_address_get(L, 0, R_X17, &doff), s = in->op == VIR_OP_MEMCPY ? a64_vir_memory_block_address_get(L, 1, R_X16, &soff) : ZR;

		if (d == R_X17 || s == R_X16)
			a64_vir_fail(L, VIRBUILD_FAILURE(memcpy_address_without_register));
		for (uint64_t o = 0; o < n;) {
			uint64_t k = n - o >= 8 ? 8 : n - o >= 4 ? 4 : n - o >= 2 ? 2 : 1;

			if (in->op == VIR_OP_MEMCPY) {
				mem(e, I_LD, log2sz(k), 0, R_X16, s, soff + (int64_t)o);
				mem(e, I_ST, log2sz(k), 0, R_X16, d, doff + (int64_t)o);
			} else {
				mem(e, I_ST, log2sz(k), 0, ZR, d, doff + (int64_t)o);
			}
			o += k;
		}
	}
}
