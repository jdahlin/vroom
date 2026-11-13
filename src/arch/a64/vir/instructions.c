/* arm64 lowering of VIR: one instruction. */

#include "lowering-internal.h"

VPRIV void a64_vir_insn_lower(a64_vir_context *L, uint32_t b, uint32_t i, uint8_t res)
{
	codegen_context *e = L->context;
	vir_function *f = L->function;
	const vir_insn *in = &f->insns[i];
	int x = in->type == VIR_TYPE_I64 || in->type == VIR_TYPE_PTR;
	int rd = res == VIR_REGISTER_NONE || a64_vir_register_is_float(res) ? R_X16 : a64_vir_general_register(res);
	int vd = res == VIR_REGISTER_NONE || !a64_vir_register_is_float(res) ? 31 : a64_vir_vector_register(res);
	int d = in->type == VIR_TYPE_F64;

	if (i > L->folded_low && i < L->folded_high)
		return;	/* (a64_vir_test_branch_fold: in the branch's test) */
	if (a64_vir_vector_lower(L, in, res))
		return;
	switch (in->op) {
	case VIR_OP_PARAM:
		a64_vir_parameter_lower(L, in, res);
		return;
	case VIR_OP_PTRADD:
		if (a64_vir_pointer_fold(L, b, i))
			return;
		__attribute__((fallthrough));
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_AND: case VIR_OP_OR: case VIR_OP_XOR: {
		if (L->chain_high && i > L->chain_low && i <= L->chain_high) {	/* a chain's and/or: the flags hold it */
			if (i == L->chain_high) {
				L->flag_condition = L->chain_condition, L->flag_value = i, L->zero_test_register = -1, L->test_bit = -1;
				L->chain_high = 0;
			}
			return;
		}
		if ((in->op == VIR_OP_PTRADD || in->op == VIR_OP_ADD || in->op == VIR_OP_SUB) && a64_vir_chain_fold(L, b, i, res))
			return;
		if (in->op == VIR_OP_AND && a64_vir_test_branch_fold(L, b, i))
			return;
		static const uint8_t rr[] = { [VIR_OP_ADD] = I_ADD, [VIR_OP_SUB] = I_SUB, [VIR_OP_AND] = I_AND, [VIR_OP_OR] = I_ORR,
					      [VIR_OP_XOR] = I_EOR, [VIR_OP_PTRADD] = I_ADD };
		static const uint8_t ri[] = { [VIR_OP_ADD] = I_ADDI, [VIR_OP_SUB] = I_SUBI, [VIR_OP_AND] = I_ANDI, [VIR_OP_OR] = I_ORRI,
					      [VIR_OP_XOR] = I_EORI, [VIR_OP_PTRADD] = I_ADDI };
		int a = a64_vir_operand_general_register(L, 0, R_X16);

		if (L->operand_registers[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(f, in->b))
			opi(e, ri[in->op], x, rd, a, in->op == VIR_OP_AND || in->op == VIR_OP_OR || in->op == VIR_OP_XOR ?
				(int64_t)a64_vir_constant_logical_immediate(&f->insns[in->b]) : a64_vir_constant_integer(&f->insns[in->b]));
		else
			op3(e, rr[in->op], x, rd, a, a64_vir_operand_general_register(L, 1, R_X17));
		return;
	}
	case VIR_OP_PSUB:
		op3(e, I_SUB, 1, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17));
		return;
	case VIR_OP_MUL:
		if (a64_vir_chain_fold(L, b, i, res))
			return;
		if (L->operand_registers[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(f, in->b) && a64_vir_constant_log2(&f->insns[in->b]) >= 0) {
			int w = x ? 64 : 32, k = a64_vir_constant_log2(&f->insns[in->b]);

			opbf(e, I_UBFM, x, rd, a64_vir_operand_general_register(L, 0, R_X16), (w - k) & (w - 1), w - 1 - k);
			return;
		}
		op4(e, I_MADD, x, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17), ZR);
		return;
	case VIR_OP_SDIV: case VIR_OP_UDIV:
		op3(e, in->op == VIR_OP_SDIV ? I_SDIV : I_UDIV, x, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17));
		return;
	case VIR_OP_SREM: case VIR_OP_UREM: {
		int a = a64_vir_operand_general_register(L, 0, R_X16), c = a64_vir_operand_general_register(L, 1, R_X17);

		if (a == R_X16 || c == R_X16)
			a64_vir_fail(L, VIRBUILD_FAILURE(constant_remainder));
		op3(e, in->op == VIR_OP_SREM ? I_SDIV : I_UDIV, x, R_X16, a, c);
		op4(e, I_MSUB, x, rd, R_X16, c, a);
		return;
	}
	case VIR_OP_SHL: case VIR_OP_LSHR: case VIR_OP_ASHR: {
		if (in->op == VIR_OP_SHL && a64_vir_chain_fold(L, b, i, res))
			return;
		if (in->op != VIR_OP_SHL && a64_vir_test_branch_fold(L, b, i))
			return;
		int a = a64_vir_operand_general_register(L, 0, R_X16), w = x ? 64 : 32;

		if (L->operand_registers[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(f, in->b)) {
			int s = (int)(a64_vir_constant_integer(&f->insns[in->b]) & (w - 1));

			if (in->op == VIR_OP_SHL)
				opbf(e, I_UBFM, x, rd, a, (w - s) & (w - 1), w - 1 - s);
			else
				opbf(e, in->op == VIR_OP_LSHR ? I_UBFM : I_SBFM, x, rd, a, s, w - 1);
		} else {
			op3(e, in->op == VIR_OP_SHL ? I_LSLV : in->op == VIR_OP_LSHR ? I_LSRV : I_ASRV, x, rd, a, a64_vir_operand_general_register(L, 1, R_X17));
		}
		return;
	}
	case VIR_OP_UMULHI: case VIR_OP_SMULHI:
		op3(e, in->op == VIR_OP_UMULHI ? I_UMULH : I_SMULH, 1, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17));
		return;
	case VIR_OP_NEG:
		op3(e, I_SUB, x, rd, ZR, a64_vir_operand_general_register(L, 0, R_X16));
		return;
	case VIR_OP_NOT:
		op3(e, I_ORN, x, rd, ZR, a64_vir_operand_general_register(L, 0, R_X16));
		return;
	case VIR_OP_CLZ:
		op3(e, I_CLZ, x, rd, a64_vir_operand_general_register(L, 0, R_X16), 0);
		return;
	case VIR_OP_CTZ:
		op3(e, I_RBIT, x, rd, a64_vir_operand_general_register(L, 0, R_X16), 0);
		op3(e, I_CLZ, x, rd, rd, 0);
		return;
	case VIR_OP_POPCNT:
		word(e, f_in(1, 31, a64_vir_operand_general_register(L, 0, R_X16)));
		word(e, 0x0e205800u | 31u << 5 | 31u);	/* cnt v31.8b, v31.8b */
		word(e, 0x0e31b800u | 31u << 5 | 31u);	/* addv b31, v31.8b */
		word(e, f_out(0, rd, 31));
		return;
	case VIR_OP_BSWAP:
		op3(e, I_REV, x, rd, a64_vir_operand_general_register(L, 0, R_X16), 0);
		return;
	case VIR_OP_EXT: {
		if (a64_vir_chain_fold(L, b, i, res))
			return;
		int w = VIR_INSN_FLAG_WIDTH_LOG2(in->flags), a = a64_vir_operand_general_register(L, 0, R_X16);

		if (w == 3 || (w == 2 && (!x || L->low32[i])))
			mov(e, 1, rd, a);	/* (ext.4 whose low half alone is read: a copy) */
		else if ((in->flags & VIR_INSN_FLAG_MS) && L->low32[i])	/* only its low half is read: the i32 form */
			opbf(e, I_SBFM, 0, rd, a, 0, w == 2 ? 31 : (8 << w) - 1);
		else if (in->flags & VIR_INSN_FLAG_MS)
			opbf(e, I_SBFM, x, rd, a, 0, (8 << w) - 1);
		else if (w == 2)
			op3(e, I_MOV, 0, rd, a, 0);
		else
			opbf(e, I_UBFM, 0, rd, a, 0, (8 << w) - 1);
		return;
	}
	case VIR_OP_TRUNC:	/* an i32's upper half is never read: a copy (ext.4u clears it) */
		mov(e, 1, rd, a64_vir_operand_general_register(L, 0, R_X16));
		return;
	case VIR_OP_P2I: case VIR_OP_I2P:	/* (unused: their users read the operand) */
		return;
	case VIR_OP_ICMP:
	case VIR_OP_FCMP: {
		int cc;

		if (in->op == VIR_OP_ICMP && L->chain_high && i > L->chain_low && i < L->chain_high) {
			a64_vir_compare_chained_lower(L, in, f->insns[i + 1].op == VIR_OP_OR);
			return;
		}
		if (in->op == VIR_OP_ICMP && !L->chain_high && a64_vir_compare_chain_lower(L, b, i)) {
			L->chain_condition = a64_vir_integer_condition_codes[a64_vir_integer_compare_lower(L, in)];
			return;
		}
		bool fuse = L->uses[i] == 1 && L->allocation->slot[i] == VIR_BLOCK_NONE && i + 1 < f->blocks[b].first + f->blocks[b].insn_count &&
			    (f->insns[i + 1].op == VIR_OP_BRIF || f->insns[i + 1].op == VIR_OP_SELECT) && f->insns[i + 1].a == i;

		/* against 0 for a branch: x == 0, x != 0, x <=u 0, x >u 0 as cbz, cbnz; x < 0, x >= 0 as
		 * tbnz, tbz of the sign bit; x <= -1, x > -1 too (when no edit in between writes x) */
		if (fuse && in->op == VIR_OP_ICMP && f->insns[i + 1].op == VIR_OP_BRIF && L->operand_registers[0] != VIR_REGISTER_NONE &&
		    L->operand_registers[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(f, in->b) && a64_vir_register_is_kept(L, L->operand_registers[0], i + 1)) {
			int64_t k = a64_vir_constant_integer(&f->insns[in->b]);
			int c = in->flags & 15, cz = -1, cb = -1;

			if (!k)
				cz = c == 0 || c == 7 ? C_EQ : c == 1 || c == 8 ? C_NE : -1, cb = c == 2 ? C_NE : c == 5 ? C_EQ : -1;
			else if (k == -1)
				cb = c == 3 ? C_NE : c == 4 ? C_EQ : -1;
			if (cz >= 0 || cb >= 0) {
				L->flag_condition = cz >= 0 ? cz : cb;
				L->flag_value = i;
				L->zero_test_register = a64_vir_general_register(L->operand_registers[0]);
				L->zero_test_is_64_bit = a64_vir_value_is_64_bit(f, in->a);
				L->test_bit = cb >= 0 ? (L->zero_test_is_64_bit ? 63 : 31) : -1;
				return;
			}
		}
		if (in->op == VIR_OP_ICMP) {
			cc = a64_vir_integer_condition_codes[a64_vir_integer_compare_lower(L, in)];
		} else {
			a64_vir_float_compare_lower(L, in);
			cc = a64_vir_float_condition_codes[in->flags & 7];
		}
		/* used once, by the next instruction as its condition: that reads the flags */
		if (cc >= 0 && fuse) {
			L->flag_condition = cc;
			L->flag_value = i;
			L->zero_test_register = -1, L->test_bit = -1;
			return;
		}
		if (res == VIR_REGISTER_NONE)
			return;
		if (cc >= 0) {
			cset(e, rd, cc);
		} else {	/* one: less or greater */
			cset(e, rd, C_MI);
			op3s(e, I_CSINC, 0, rd, rd, ZR, 0, 0);
			e->records[e->record_count - 1].cond = C_LE;
		}
		return;
	}
	case VIR_OP_SELECT: {
		int cc = a64_vir_condition_flags_get(L, in);

		if (in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64)
			word(e, 0x1e200c00u | (unsigned)d << 22 | (unsigned)a64_vir_operand_float_register(L, 2) << 16 | (unsigned)cc << 12 |
				(unsigned)a64_vir_operand_float_register(L, 1) << 5 | (unsigned)vd);
		else
			op3s(e, I_CSEL, x, rd, a64_vir_operand_general_register(L, 1, R_X16), a64_vir_operand_general_register(L, 2, R_X17), 0, 0), e->records[e->record_count - 1].cond = (uint8_t)cc;
		return;
	}
	case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV: {
		static const uint32_t op[] = { 0x1e202800u, 0x1e203800u, 0x1e200800u, 0x1e201800u };

		word(e, op[in->op - VIR_OP_FADD] | (unsigned)d << 22 | (unsigned)a64_vir_operand_float_register(L, 1) << 16 | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VIR_OP_FMA:	/* fmadd, fmsub (the product negated), fnmsub (the addend), fnmadd (both) */
		word(e, 0x1f000000u | (unsigned)d << 22 | (unsigned)((in->flags & VIR_INSN_FLAG_NEGC) != 0) << 21 | (unsigned)a64_vir_operand_float_register(L, 1) << 16 |
			(unsigned)(!(in->flags & VIR_INSN_FLAG_NEGP) != !(in->flags & VIR_INSN_FLAG_NEGC)) << 15 | (unsigned)a64_vir_operand_float_register(L, 2) << 10 |
			(unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_FNEG: case VIR_OP_FABS: case VIR_OP_FSQRT: {
		uint32_t op = in->op == VIR_OP_FNEG ? 0x1e214000u : in->op == VIR_OP_FABS ? 0x1e20c000u : 0x1e21c000u;

		word(e, op | (unsigned)d << 22 | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VIR_OP_FEXT:
		word(e, 0x1e22c000u | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_FTRUNC:
		word(e, 0x1e624000u | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_S2F: case VIR_OP_U2F:
		word(e, (in->op == VIR_OP_S2F ? 0x1e220000u : 0x1e230000u) | (unsigned)a64_vir_value_is_64_bit(f, in->a) << 31 | (unsigned)d << 22 |
			(unsigned)a64_vir_operand_general_register(L, 0, R_X16) << 5 | (unsigned)vd);
		return;
	case VIR_OP_F2S: case VIR_OP_F2U:
		word(e, (in->op == VIR_OP_F2S ? 0x1e380000u : 0x1e390000u) | (unsigned)x << 31 |
			(unsigned)(f->insns[in->a].type == VIR_TYPE_F64) << 22 | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)rd);
		return;
	case VIR_OP_BITCAST:
		if (in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64)
			word(e, f_in(d, vd, a64_vir_operand_general_register(L, 0, R_X16)));
		else
			word(e, f_out(x, rd, a64_vir_operand_float_register(L, 0)));
		return;
	case VIR_OP_LOAD:
		a64_vir_load_lower(L, i, in, res);
		return;
	case VIR_OP_STORE:
		a64_vir_store_lower(L, in);
		return;
	case VIR_OP_MEMCPY: case VIR_OP_MEMSET:
		a64_vir_memory_block_lower(L, in);
		return;
	case VIR_OP_ALOAD:	/* ldar: sequentially consistent with stlr */
		word(e, 0x08dffc00u | (unsigned)VIR_INSN_FLAG_WIDTH_LOG2(in->flags) << 30 | (unsigned)a64_vir_operand_general_register(L, 0, R_X16) << 5 | (unsigned)rd);
		return;
	case VIR_OP_ASTORE: {
		int p = a64_vir_operand_general_register(L, 0, R_X16), v = a64_vir_operand_general_register(L, 1, R_X17);

		word(e, 0x089ffc00u | (unsigned)VIR_INSN_FLAG_WIDTH_LOG2(in->flags) << 30 | (unsigned)p << 5 | (unsigned)v);
		return;
	}
	case VIR_OP_AFENCE:
		word(e, 0xd5033bbfu);	/* dmb ish */
		return;
	case VIR_OP_BARRIER:
		return;
	case VIR_OP_CALL:
		a64_vir_call_lower(L, b, i, in);
		return;
	case VIR_OP_VA_START: {	/* Apple's: the first unnamed argument, after the named ones on the stack */
		a64_vir_argument none;
		int32_t stk;
		codegen_lvalue l = a64_vir_address_get(L, in->flags, R_X16);
		int t = l.kind == CODEGEN_LVALUE_REGISTER && l.register_number == R_X16 ? R_X17 : R_X16;

		if (!CODEGEN_IS_DARWIN(e->compile_context))
			a64_vir_fail(L, VIRBUILD_FAILURE(variadic_start_lowering));
		a64_vir_arguments_assign(true, &f->extra[f->signature], &none, 0, &stk);
		if (l.kind == CODEGEN_LVALUE_SYMBOL) {
			addr_of_ref(e, R_X17, l.reference, l.offset);
			l = (codegen_lvalue){ .kind = CODEGEN_LVALUE_REGISTER, .register_number = R_X17 };
		}
		opi(e, I_ADDI, 1, t, R_FP, 16 + ((stk + 7) & ~7));
		mem(e, I_ST, 3, 0, t, l.kind == CODEGEN_LVALUE_FRAME ? FRAME : l.register_number, l.offset);
		return;
	}
	case VIR_OP_BR: {
		uint32_t g = L->allocation->edge_indices[L->allocation->slot_offsets[b]], t = a64_vir_block_chase(L, in->a);

		if (g != VIR_BLOCK_NONE && L->allocation->edge[g].count)
			a64_vir_edge_moves_emit(L, g);
		a64_vir_goto_emit(L, b, t);
		return;
	}
	case VIR_OP_BRIF: {	/* the jump that remains goes to what follows: the next block or the first edge's moves */
		a64_vir_condition cc = a64_vir_branch_condition_get(L, in);
		bool tp = a64_vir_edge_is_plain(L, b, 0), fp = a64_vir_edge_is_plain(L, b, 1);

		if (tp && fp) {
			uint32_t t = a64_vir_block_chase(L, in->b), el = a64_vir_block_label(L, a64_vir_block_chase(L, in->c));

			if (t == a64_vir_block_fallthrough(L)) {
				a64_vir_branch_conditional_emit(e, cc, true, el);
				jump(e, a64_vir_block_label(L, t));
			} else {
				a64_vir_branch_conditional_emit(e, cc, false, a64_vir_block_label(L, t));
				a64_vir_goto_emit(L, b, a64_vir_block_chase(L, in->c));
			}
		} else if (fp) {
			a64_vir_branch_conditional_emit(e, cc, true, a64_vir_block_label(L, a64_vir_block_chase(L, in->c)));
			jump(e, a64_vir_successor_label(L, b, 0, in->b));
		} else {
			uint32_t el = a64_vir_successor_label(L, b, 1, in->c), tl = a64_vir_successor_label(L, b, 0, in->b);

			a64_vir_branch_conditional_emit(e, cc, false, tl);
			jump(e, el);
		}
		return;
	}
	case VIR_OP_SWITCH:
		a64_vir_switch_lower(L, b, in);
		return;
	case VIR_OP_RET:
		a64_vir_return_lower(L);
		return;
	case VIR_OP_TRAP:
		put(e, I_BRK)->imm = 1;
		e->is_reachable = false;
		return;
	default: {
		snprintf(L->context->vir_builder->failure_detail, sizeof L->context->vir_builder->failure_detail, "%s", vir_opcode_table[in->op].name ? vir_opcode_table[in->op].name : "?");
		a64_vir_fail(L, VIRBUILD_FAILURE(unsupported_instruction));
	}
	}
}
