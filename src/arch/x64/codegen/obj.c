/* x86 records -> structured assembler operands. Encoding, relocations and
 * branch relaxation belong to the existing assembler, shared with -S input. */
#include "x64-internal.h"
#include "../as/operand.h"

VPRIV bool x64_record_object_prepare(codegen_context *e, const record *i, record *out)
{
	switch (i->opcode) {
	case X64_OP_LABEL: case X64_OP_MOV: case X64_OP_MOVI: case X64_OP_LD: case X64_OP_LDX:
	case X64_OP_ST: case X64_OP_STX: case X64_OP_LEA: case X64_OP_LEASCALE:
	case X64_OP_ADD: case X64_OP_SUB: case X64_OP_AND: case X64_OP_OR: case X64_OP_XOR:
	case X64_OP_IMUL: case X64_OP_ADC: case X64_OP_SBB:
	case X64_OP_ADDI: case X64_OP_SUBI: case X64_OP_ANDI: case X64_OP_ORI: case X64_OP_XORI:
	case X64_OP_SHLI: case X64_OP_SHRI: case X64_OP_SARI: case X64_OP_SHL: case X64_OP_SHR: case X64_OP_SAR:
	case X64_OP_IMULI: case X64_OP_NEG: case X64_OP_NOT: case X64_OP_CMP: case X64_OP_CMPI:
	case X64_OP_TEST: case X64_OP_CMOV: case X64_OP_SETCC: case X64_OP_JMP: case X64_OP_JCC:
	case X64_OP_PUSH: case X64_OP_POP: case X64_OP_CQO:
	case X64_OP_LEAL: case X64_OP_MFENCE: case X64_OP_UD2: case X64_OP_BSF: case X64_OP_BSR:
	case X64_OP_MULW: case X64_OP_SMULW: case X64_OP_DIV: case X64_OP_IDIV: case X64_OP_EXT:
	case X64_OP_FADD: case X64_OP_FSUB: case X64_OP_FMUL: case X64_OP_FDIV: case X64_OP_FXOR:
		break;
	case X64_OP_CALLR: case X64_OP_JMPR:
		if (e->unit->options->xcf & 4) return false;
		break;
	case X64_OP_CALL:
		if (CODEGEN_IS_WIN64(e->compile_context) && codegen_symbol_needs_got(e->compile_context, i->reference)) return false;	/* (callq *__imp_f(%rip): text) */
		break;
	default: return false;
	}
	*out = *i;
	if (i->opcode == X64_OP_LD || i->opcode == X64_OP_LDX || i->opcode == X64_OP_ST || i->opcode == X64_OP_STX || i->opcode == X64_OP_LEA) {
		if (i->registers[1] == X64_REGISTER_FRAME) {
			int64_t off = i->offset + e->x64_context->frame_size - 8 - e->x64_context->stack_pointer_delta;
			/* Preserve the textual path for a displacement wider than rec.off. */
			if (off < INT32_MIN || off > INT32_MAX) return false;
			out->registers[1] = RSP; out->offset = (int32_t)off;
		} else if (i->registers[1] == RBP && i->offset < 0 && e->walk_state->frame_alignment > 16)
			out->registers[1] = R12;
		else if (i->registers[1] == RBP && i->offset < 0)
			out->offset = (int32_t)(i->offset - e->x64_context->wsave);	/* (the Windows frame's save area, print.c) */
	}
	return true;
}

static void x64_encode_register(XOp *operand, int r, int size)
{
	memset(operand, 0, sizeof *operand);
	operand->kind = XK_REG;
	if (X64_REGISTER_IS_XMM(r)) {
		operand->cls = XR_XMM;
		operand->reg = r - XMM0;
		operand->size = 16;
	} else {
		operand->cls = XR_GPR;
		operand->reg = r;
		operand->size = size;
		operand->needrex = size == 1 && r >= 4;
	}
}

static void x64_encode_immediate(XOp *operand, int64_t v)
{
	memset(operand, 0, sizeof *operand);
	operand->kind = XK_IMM;
	operand->v.c = v;
	operand->hasv = 1;
}

static uint32_t x64_encode_label(codegen_commit_context *R, uint32_t fid, uint32_t label)
{
	size_t index = label % (sizeof R->x64_labels / sizeof R->x64_labels[0]);
	if (R->x64_labels[index].symbol && R->x64_labels[index].function == fid && R->x64_labels[index].label == label)
		return R->x64_labels[index].symbol;
	R->name_buffer.n = 0;
	x64_label_name_append(&R->name_buffer, fid, label);
	uint32_t symbol = as_symbol_by_name(R->assembler, R->name_buffer.p, R->name_buffer.n);
	R->x64_labels[index].function = fid;
	R->x64_labels[index].label = label;
	R->x64_labels[index].symbol = symbol;
	return symbol;
}

static void x64_encode_memory(XOp *operand, codegen_commit_context *R, const record *i)
{
	memset(operand, 0, sizeof *operand);
	operand->kind = XK_MEM;
	operand->base = i->registers[1];
	operand->index = -1;
	operand->scale = 1;
	operand->hasv = 1;
	if (i->registers[1] == RIP) {
		operand->base = 16;
		operand->v = as_symbol_value(R->assembler, codegen_commit_symbol_find(R, i->reference), i->offset);
	} else {
		operand->v.c = i->offset;
	}
	if (i->opcode == X64_OP_LDX || i->opcode == X64_OP_STX) {
		operand->index = i->registers[3];
		operand->scale = 1u << i->shift;
	}
}

static int x64_encode_instruction(codegen_commit_context *R, const char *name, int sz, XOp *o, int n)
{
	char mn[32];
	size_t k = strlen(name);
	memcpy(mn, name, k);
	if (sz) mn[k++] = sz == 8 ? 'q' : sz == 4 ? 'l' : sz == 2 ? 'w' : 'b';
	mn[k] = 0;
	return as_x64_insn(R->assembler, mn, o, n);
}

VPRIV int x64_function_commit(codegen_commit_context *R, const char *p, size_t n)
{
	uint32_t fid = *(const uint32_t *)p;
	const record *in = (const record *)(p + 8);
	const record *end = in + (n - 8) / sizeof *in;
	static const enum X64Op integer_op[] = {
		[X64_OP_ADD]=X64O_ADD, [X64_OP_ADDI]=X64O_ADD, [X64_OP_SUB]=X64O_SUB, [X64_OP_SUBI]=X64O_SUB,
		[X64_OP_AND]=X64O_AND, [X64_OP_ANDI]=X64O_AND, [X64_OP_OR]=X64O_OR, [X64_OP_ORI]=X64O_OR,
		[X64_OP_XOR]=X64O_XOR, [X64_OP_XORI]=X64O_XOR, [X64_OP_ADC]=X64O_ADC, [X64_OP_SBB]=X64O_SBB,
	};
	static const char *const shift[] = {
		[X64_OP_SHLI]="shl", [X64_OP_SHRI]="shr", [X64_OP_SARI]="sar", [X64_OP_SHL]="shl", [X64_OP_SHR]="shr", [X64_OP_SAR]="sar",
	};
	for (const record *i = in; i != end; i++) {
		int sz = i->size, count = 2, op = -1;
		const char *mn = NULL;
		char name[24];
		XOp o[3];
		if (i->opcode == X64_OP_LABEL) {
			if (as_label_define_symbol(R->assembler, x64_encode_label(R, fid, i->label))) return -1;
			continue;
		}
		if (i->opcode == X64_OP_JMP || i->opcode == X64_OP_JCC) {
			uint32_t target = x64_encode_label(R, fid, i->label);
			if (as_x64_direct_branch(R->assembler, i->opcode == X64_OP_JMP ? 16 : i->condition, target, XM_NONE))
				return -1;
			continue;
		}
		if (i->opcode == X64_OP_CALL) {
			uint32_t target = codegen_commit_symbol_find(R, i->reference);
			bool plt = R->compile_context->o->pic && (CODEGEN_REFERENCE_KIND(i->reference) == CODEGEN_REFERENCE_NAME ||
			                                          codegen_symbol_needs_got(R->compile_context, i->reference));
			if (as_x64_direct_branch(R->assembler, -1, target, plt ? XM_PLT : XM_NONE))
				return -1;
			continue;
		}
		/* Most binary instructions write operand 1. Select its register and
		 * width here, then build it once after decoding the record. */
		int dst = i->registers[0], dst_size = sz;
		switch (i->opcode) {
		case X64_OP_MOV:
			x64_encode_register(&o[0], i->registers[1], sz);
			mn = "mov";
			if (!X64_REGISTER_IS_XMM(i->registers[0]) && !X64_REGISTER_IS_XMM(i->registers[1])) op=X64O_MOV;
			if (X64_REGISTER_IS_XMM(i->registers[0]) && X64_REGISTER_IS_XMM(i->registers[1])) { mn="movaps"; sz=0; }
			else if (X64_REGISTER_IS_XMM(i->registers[0]) || X64_REGISTER_IS_XMM(i->registers[1])) { mn=sz==4?"movd":"movq"; sz=0; }
			break;
		case X64_OP_MOVI:
			if (as_x64_mov_immediate(R->assembler, dst, sz, i->immediate)) return -1;
			continue;
		case X64_OP_LD: case X64_OP_LDX: case X64_OP_EXT:
			if (i->opcode == X64_OP_EXT) x64_encode_register(&o[0], i->registers[1], sz);
			else x64_encode_memory(&o[0], R, i);
			if (X64_REGISTER_IS_XMM(i->registers[0])) {
				mn = sz == 4 ? "movss" : sz == 8 ? "movsd" : "movups";
				sz = 0;
			} else {
				dst_size = i->condition || sz == 8 ? 8 : 4;
				op = sz == dst_size ? X64O_MOV : i->condition ? X64O_MOVSX : X64O_MOVZX;
			}
			break;
		case X64_OP_ST: case X64_OP_STX:
			dst = -1; /* memory destination */
			mn="mov"; x64_encode_register(&o[0], i->registers[2],sz); x64_encode_memory(&o[1], R, i);
			if (!X64_REGISTER_IS_XMM(i->registers[2])) op=X64O_MOV;
			if (X64_REGISTER_IS_XMM(i->registers[2])) { mn=sz==4?"movss":sz==8?"movsd":"movups"; sz=0; } break;
		case X64_OP_LEA: op=X64O_LEA; x64_encode_memory(&o[0], R, i); break;
		case X64_OP_LEASCALE:
			op=X64O_LEA; o[0]=(XOp){ .kind=XK_MEM,.base=(int8_t)i->registers[1],.index=(int8_t)i->registers[2],.scale=(uint8_t)(1u<<i->shift) }; break;
		case X64_OP_ADD: case X64_OP_SUB: case X64_OP_AND: case X64_OP_OR: case X64_OP_XOR: case X64_OP_ADC: case X64_OP_SBB:
			op = integer_op[i->opcode]; x64_encode_register(&o[0], i->registers[2], sz); break;
		case X64_OP_ADDI: case X64_OP_SUBI: case X64_OP_ANDI: case X64_OP_ORI: case X64_OP_XORI:
			op = integer_op[i->opcode]; x64_encode_immediate(&o[0], i->immediate); break;
		case X64_OP_IMUL: mn = "imul"; x64_encode_register(&o[0], i->registers[2], sz); break;
		case X64_OP_SHLI: case X64_OP_SHRI: case X64_OP_SARI: mn = shift[i->opcode]; x64_encode_immediate(&o[0], i->immediate); break;
		case X64_OP_SHL: case X64_OP_SHR: case X64_OP_SAR: mn = shift[i->opcode]; x64_encode_register(&o[0], RCX, 1); break;
		case X64_OP_IMULI:
			mn = "imul"; count = 3;
			x64_encode_register(&o[2], dst, sz);
			dst = i->registers[1];
			x64_encode_immediate(&o[0], i->immediate);
			break;
		case X64_OP_NEG: case X64_OP_NOT:
			mn = i->opcode == X64_OP_NEG ? "neg" : "not";
			x64_encode_register(&o[0], dst, sz);
			count = 1;
			break;
		case X64_OP_CMP: case X64_OP_CMPI:
			op = X64O_CMP;
			dst = i->registers[1];
			if (i->opcode == X64_OP_CMPI) x64_encode_immediate(&o[0], i->immediate);
			else x64_encode_register(&o[0], i->registers[2], sz);
			break;
		case X64_OP_TEST:
			op = X64O_TEST; dst = i->registers[1];
			x64_encode_register(&o[0], dst, sz);
			break;
		case X64_OP_CMOV:
			x64_encode_register(&o[0], i->registers[2], sz);
			x64_encode_register(&o[1], dst, sz);
			if (as_x64_condition(R->assembler, X64O_CMOV, i->condition, sz, o)) return -1;
			continue;
		case X64_OP_SETCC:
			x64_encode_register(&o[0], i->registers[0], 1);
			if (as_x64_condition(R->assembler, X64O_SETCC, i->condition, 1, o)) return -1;
			dst_size = 4;
			op = X64O_MOVZX; sz = 1; break;
		case X64_OP_LEAL:
			op = X64O_LEA;
			sz = 8;
			o[0] = (XOp){ .kind = XK_MEM, .base = 16, .index = -1, .scale = 1, .hasv = 1,
			              .v = as_symbol_value(R->assembler, x64_encode_label(R, fid, i->label), 0) };
			break;
		case X64_OP_CALLR: case X64_OP_JMPR: mn=i->opcode==X64_OP_CALLR?"callq":"jmpq";sz=0;count=1;x64_encode_register(&o[0], i->registers[1],8);o[0].star=1;break;
		case X64_OP_PUSH: case X64_OP_POP: mn=i->opcode==X64_OP_PUSH?"pushq":"popq";sz=0;count=1;x64_encode_register(&o[0], i->opcode==X64_OP_PUSH?i->registers[1]:i->registers[0],8);break;
		case X64_OP_CQO: mn=sz==8?"cqto":"cltd";sz=0;count=0;break;
		case X64_OP_MFENCE: case X64_OP_UD2: mn=i->opcode==X64_OP_MFENCE?"mfence":"ud2";sz=0;count=0;break;
		case X64_OP_BSF: case X64_OP_BSR:
			x64_encode_register(&o[0], i->registers[1], sz);
			mn = i->opcode == X64_OP_BSF ? "bsf" : "bsr";
			break;
		case X64_OP_MULW: case X64_OP_SMULW: case X64_OP_DIV: case X64_OP_IDIV:
			x64_encode_register(&o[0], i->registers[1], sz);
			mn=i->opcode==X64_OP_MULW?"mul":i->opcode==X64_OP_SMULW?"imul":i->opcode==X64_OP_DIV?"div":"idiv";count=1;break;
		case X64_OP_FADD: case X64_OP_FSUB: case X64_OP_FMUL: case X64_OP_FDIV: {
			static const char *const op[]={"add","sub","mul","div"};
			snprintf(name,sizeof name,"%s%s",op[i->opcode-X64_OP_FADD],sz==4?"ss":"sd");mn=name;sz=0;x64_encode_register(&o[0], i->registers[2],8);break;
		}
		case X64_OP_FXOR: mn="xorps";sz=0;x64_encode_register(&o[0], i->registers[2],8);break;
		default: return -1;
		}
		if (count >= 2 && dst >= 0) x64_encode_register(&o[1], dst, dst_size);
		if (op >= 0) {
			if (as_x64_op(R->assembler, (enum X64Op)op, sz ? sz : o[1].size, o)) return -1;
		} else if(x64_encode_instruction(R,mn,sz,o,count)) return -1;
	}
	return 0;
}
