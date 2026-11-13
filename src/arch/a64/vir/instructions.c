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
		static const uint8_t rr[] = { [VIR_OP_ADD] = A64_OP_ADD, [VIR_OP_SUB] = A64_OP_SUB, [VIR_OP_AND] = A64_OP_AND, [VIR_OP_OR] = A64_OP_ORR,
					      [VIR_OP_XOR] = A64_OP_EOR, [VIR_OP_PTRADD] = A64_OP_ADD };
		static const uint8_t ri[] = { [VIR_OP_ADD] = A64_OP_ADDI, [VIR_OP_SUB] = A64_OP_SUBI, [VIR_OP_AND] = A64_OP_ANDI, [VIR_OP_OR] = A64_OP_ORRI,
					      [VIR_OP_XOR] = A64_OP_EORI, [VIR_OP_PTRADD] = A64_OP_ADDI };
		int a = a64_vir_operand_general_register(L, 0, R_X16);

		if (L->operand_registers[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(f, in->b))
			a64_record_emit_immediate(e, ri[in->op], x, rd, a, in->op == VIR_OP_AND || in->op == VIR_OP_OR || in->op == VIR_OP_XOR ?
				(int64_t)a64_vir_constant_logical_immediate(&f->insns[in->b]) : a64_vir_constant_integer(&f->insns[in->b]));
		else
			a64_record_emit_three_registers(e, rr[in->op], x, rd, a, a64_vir_operand_general_register(L, 1, R_X17));
		return;
	}
	case VIR_OP_PSUB:
		a64_record_emit_three_registers(e, A64_OP_SUB, 1, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17));
		return;
	case VIR_OP_MUL:
		if (a64_vir_chain_fold(L, b, i, res))
			return;
		if (L->operand_registers[1] == VIR_REGISTER_NONE && a64_vir_value_is_integer_constant(f, in->b) && a64_vir_constant_log2(&f->insns[in->b]) >= 0) {
			int w = x ? 64 : 32, k = a64_vir_constant_log2(&f->insns[in->b]);

			a64_record_emit_bitfield(e, A64_OP_UBFM, x, rd, a64_vir_operand_general_register(L, 0, R_X16), (w - k) & (w - 1), w - 1 - k);
			return;
		}
		a64_record_emit_four_registers(e, A64_OP_MADD, x, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17), ZR);
		return;
	case VIR_OP_SDIV: case VIR_OP_UDIV:
		a64_record_emit_three_registers(e, in->op == VIR_OP_SDIV ? A64_OP_SDIV : A64_OP_UDIV, x, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17));
		return;
	case VIR_OP_SREM: case VIR_OP_UREM: {
		int a = a64_vir_operand_general_register(L, 0, R_X16), c = a64_vir_operand_general_register(L, 1, R_X17);

		if (a == R_X16 || c == R_X16)
			a64_vir_fail(L, VIRBUILD_FAILURE(constant_remainder));
		a64_record_emit_three_registers(e, in->op == VIR_OP_SREM ? A64_OP_SDIV : A64_OP_UDIV, x, R_X16, a, c);
		a64_record_emit_four_registers(e, A64_OP_MSUB, x, rd, R_X16, c, a);
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
				a64_record_emit_bitfield(e, A64_OP_UBFM, x, rd, a, (w - s) & (w - 1), w - 1 - s);
			else
				a64_record_emit_bitfield(e, in->op == VIR_OP_LSHR ? A64_OP_UBFM : A64_OP_SBFM, x, rd, a, s, w - 1);
		} else {
			a64_record_emit_three_registers(e, in->op == VIR_OP_SHL ? A64_OP_LSLV : in->op == VIR_OP_LSHR ? A64_OP_LSRV : A64_OP_ASRV, x, rd, a, a64_vir_operand_general_register(L, 1, R_X17));
		}
		return;
	}
	case VIR_OP_UMULHI: case VIR_OP_SMULHI:
		a64_record_emit_three_registers(e, in->op == VIR_OP_UMULHI ? A64_OP_UMULH : A64_OP_SMULH, 1, rd, a64_vir_operand_general_register(L, 0, R_X16), a64_vir_operand_general_register(L, 1, R_X17));
		return;
	case VIR_OP_NEG:
		a64_record_emit_three_registers(e, A64_OP_SUB, x, rd, ZR, a64_vir_operand_general_register(L, 0, R_X16));
		return;
	case VIR_OP_NOT:
		a64_record_emit_three_registers(e, A64_OP_ORN, x, rd, ZR, a64_vir_operand_general_register(L, 0, R_X16));
		return;
	case VIR_OP_CLZ:
		a64_record_emit_three_registers(e, A64_OP_CLZ, x, rd, a64_vir_operand_general_register(L, 0, R_X16), 0);
		return;
	case VIR_OP_CTZ:
		a64_record_emit_three_registers(e, A64_OP_RBIT, x, rd, a64_vir_operand_general_register(L, 0, R_X16), 0);
		a64_record_emit_three_registers(e, A64_OP_CLZ, x, rd, rd, 0);
		return;
	case VIR_OP_POPCNT:
		a64_record_emit_word(e, a64_word_fmov_to_vector(1, 31, a64_vir_operand_general_register(L, 0, R_X16)));
		a64_record_emit_word(e, 0x0e205800u | 31u << 5 | 31u);	/* cnt v31.8b, v31.8b */
		a64_record_emit_word(e, 0x0e31b800u | 31u << 5 | 31u);	/* addv b31, v31.8b */
		a64_record_emit_word(e, a64_word_fmov_from_vector(0, rd, 31));
		return;
	case VIR_OP_BSWAP:
		a64_record_emit_three_registers(e, A64_OP_REV, x, rd, a64_vir_operand_general_register(L, 0, R_X16), 0);
		return;
	case VIR_OP_EXT: {
		if (a64_vir_chain_fold(L, b, i, res))
			return;
		int w = VIR_INSN_FLAG_WIDTH_LOG2(in->flags), a = a64_vir_operand_general_register(L, 0, R_X16);

		if (w == 3 || (w == 2 && (!x || L->low32[i])))
			a64_record_emit_move(e, 1, rd, a);	/* (ext.4 whose low half alone is read: a copy) */
		else if ((in->flags & VIR_INSN_FLAG_MS) && L->low32[i])	/* only its low half is read: the i32 form */
			a64_record_emit_bitfield(e, A64_OP_SBFM, 0, rd, a, 0, w == 2 ? 31 : (8 << w) - 1);
		else if (in->flags & VIR_INSN_FLAG_MS)
			a64_record_emit_bitfield(e, A64_OP_SBFM, x, rd, a, 0, (8 << w) - 1);
		else if (w == 2)
			a64_record_emit_three_registers(e, A64_OP_MOV, 0, rd, a, 0);
		else
			a64_record_emit_bitfield(e, A64_OP_UBFM, 0, rd, a, 0, (8 << w) - 1);
		return;
	}
	case VIR_OP_TRUNC:	/* an i32's upper half is never read: a copy (ext.4u clears it) */
		a64_record_emit_move(e, 1, rd, a64_vir_operand_general_register(L, 0, R_X16));
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
				cz = c == 0 || c == 7 ? A64_CONDITION_EQ : c == 1 || c == 8 ? A64_CONDITION_NE : -1, cb = c == 2 ? A64_CONDITION_NE : c == 5 ? A64_CONDITION_EQ : -1;
			else if (k == -1)
				cb = c == 3 ? A64_CONDITION_NE : c == 4 ? A64_CONDITION_EQ : -1;
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
			a64_record_emit_condition_set(e, rd, cc);
		} else {	/* one: less or greater */
			a64_record_emit_condition_set(e, rd, A64_CONDITION_MI);
			a64_record_emit_three_registers_shifted(e, A64_OP_CSINC, 0, rd, rd, ZR, 0, 0);
			e->records[e->record_count - 1].condition = A64_CONDITION_LE;
		}
		return;
	}
	case VIR_OP_SELECT: {
		int cc = a64_vir_condition_flags_get(L, in);

		if (in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64)
			a64_record_emit_word(e, 0x1e200c00u | (unsigned)d << 22 | (unsigned)a64_vir_operand_float_register(L, 2) << 16 | (unsigned)cc << 12 |
				(unsigned)a64_vir_operand_float_register(L, 1) << 5 | (unsigned)vd);
		else
			a64_record_emit_three_registers_shifted(e, A64_OP_CSEL, x, rd, a64_vir_operand_general_register(L, 1, R_X16), a64_vir_operand_general_register(L, 2, R_X17), 0, 0), e->records[e->record_count - 1].condition = (uint8_t)cc;
		return;
	}
	case VIR_OP_FADD: case VIR_OP_FSUB: case VIR_OP_FMUL: case VIR_OP_FDIV: {
		static const uint32_t op[] = { 0x1e202800u, 0x1e203800u, 0x1e200800u, 0x1e201800u };

		a64_record_emit_word(e, op[in->op - VIR_OP_FADD] | (unsigned)d << 22 | (unsigned)a64_vir_operand_float_register(L, 1) << 16 | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VIR_OP_FMA:	/* fmadd, fmsub (the product negated), fnmsub (the addend), fnmadd (both) */
		a64_record_emit_word(e, 0x1f000000u | (unsigned)d << 22 | (unsigned)((in->flags & VIR_INSN_FLAG_NEGC) != 0) << 21 | (unsigned)a64_vir_operand_float_register(L, 1) << 16 |
			(unsigned)(!(in->flags & VIR_INSN_FLAG_NEGP) != !(in->flags & VIR_INSN_FLAG_NEGC)) << 15 | (unsigned)a64_vir_operand_float_register(L, 2) << 10 |
			(unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_FNEG: case VIR_OP_FABS: case VIR_OP_FSQRT: {
		uint32_t op = in->op == VIR_OP_FNEG ? 0x1e214000u : in->op == VIR_OP_FABS ? 0x1e20c000u : 0x1e21c000u;

		a64_record_emit_word(e, op | (unsigned)d << 22 | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	}
	case VIR_OP_FEXT:
		a64_record_emit_word(e, 0x1e22c000u | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_FTRUNC:
		a64_record_emit_word(e, 0x1e624000u | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)vd);
		return;
	case VIR_OP_S2F: case VIR_OP_U2F:
		a64_record_emit_word(e, (in->op == VIR_OP_S2F ? 0x1e220000u : 0x1e230000u) | (unsigned)a64_vir_value_is_64_bit(f, in->a) << 31 | (unsigned)d << 22 |
			(unsigned)a64_vir_operand_general_register(L, 0, R_X16) << 5 | (unsigned)vd);
		return;
	case VIR_OP_F2S: case VIR_OP_F2U:
		a64_record_emit_word(e, (in->op == VIR_OP_F2S ? 0x1e380000u : 0x1e390000u) | (unsigned)x << 31 |
			(unsigned)(f->insns[in->a].type == VIR_TYPE_F64) << 22 | (unsigned)a64_vir_operand_float_register(L, 0) << 5 | (unsigned)rd);
		return;
	case VIR_OP_BITCAST:
		if (in->type == VIR_TYPE_F32 || in->type == VIR_TYPE_F64)
			a64_record_emit_word(e, a64_word_fmov_to_vector(d, vd, a64_vir_operand_general_register(L, 0, R_X16)));
		else
			a64_record_emit_word(e, a64_word_fmov_from_vector(x, rd, a64_vir_operand_float_register(L, 0)));
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
		a64_record_emit_word(e, 0x08dffc00u | (unsigned)VIR_INSN_FLAG_WIDTH_LOG2(in->flags) << 30 | (unsigned)a64_vir_operand_general_register(L, 0, R_X16) << 5 | (unsigned)rd);
		return;
	case VIR_OP_ASTORE: {
		int p = a64_vir_operand_general_register(L, 0, R_X16), v = a64_vir_operand_general_register(L, 1, R_X17);

		a64_record_emit_word(e, 0x089ffc00u | (unsigned)VIR_INSN_FLAG_WIDTH_LOG2(in->flags) << 30 | (unsigned)p << 5 | (unsigned)v);
		return;
	}
	case VIR_OP_AFENCE:
		a64_record_emit_word(e, 0xd5033bbfu);	/* dmb ish */
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
			a64_reference_address_emit(e, R_X17, l.reference, l.offset);
			l = (codegen_lvalue){ .kind = CODEGEN_LVALUE_REGISTER, .register_number = R_X17 };
		}
		a64_record_emit_immediate(e, A64_OP_ADDI, 1, t, R_FP, 16 + ((stk + 7) & ~7));
		a64_record_emit_memory(e, A64_OP_ST, 3, 0, t, l.kind == CODEGEN_LVALUE_FRAME ? A64_REGISTER_FRAME : l.register_number, l.offset);
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
				a64_jump_emit(e, a64_vir_block_label(L, t));
			} else {
				a64_vir_branch_conditional_emit(e, cc, false, a64_vir_block_label(L, t));
				a64_vir_goto_emit(L, b, a64_vir_block_chase(L, in->c));
			}
		} else if (fp) {
			a64_vir_branch_conditional_emit(e, cc, true, a64_vir_block_label(L, a64_vir_block_chase(L, in->c)));
			a64_jump_emit(e, a64_vir_successor_label(L, b, 0, in->b));
		} else {
			uint32_t el = a64_vir_successor_label(L, b, 1, in->c), tl = a64_vir_successor_label(L, b, 0, in->b);

			a64_vir_branch_conditional_emit(e, cc, false, tl);
			a64_jump_emit(e, el);
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
		a64_record_emit(e, A64_OP_BRK)->immediate = 1;
		e->is_reachable = false;
		return;
	default: {
		snprintf(L->context->vir_builder->failure_detail, sizeof L->context->vir_builder->failure_detail, "%s", vir_opcode_table[in->op].name ? vir_opcode_table[in->op].name : "?");
		a64_vir_fail(L, VIRBUILD_FAILURE(unsupported_instruction));
	}
	}
}
